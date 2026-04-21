#include "LensFlare.h"

#include "../../../../../Scene/SceneManager.h"
#include "../../../../Camera/CameraBase.h"

namespace
{
	// カメラ前方の少し先を仮想的な太陽位置として扱う距離。
	constexpr float kSunDistance = 400.0f;
	// 明るさを急変させず、自然に追従させる補間係数。
	constexpr float kFadeSpeed = 0.18f;
}

// フレア描画に必要なテクスチャを一度だけ読み込む。
void LensFlare::Init()
{
	if (!m_spTexture)
	{
		m_spTexture = std::make_shared<KdTexture>("Asset/Textures/LensFlare/flare_sheet.png");
	}
}

// ライト方向をスクリーン座標へ変換し、表示可能かどうかと明るさを更新する。
void LensFlare::PostUpdate()
{
	float targetIntensity = 0.0f;

	const std::shared_ptr<CameraBase> spCamera = m_wpCamera.lock();
	if (spCamera && m_spTexture)
	{
		const std::shared_ptr<KdCamera>& spKdCamera = spCamera->GetCamera();
		if (spKdCamera)
		{
			spKdCamera->SetCameraMatrix(spCamera->GetMatrix());

			// 平行光源の逆方向にある位置を、画面上のフレア発生源として使う。
			Math::Vector3 lightDir = KdShaderManager::Instance().GetLightCB().DirLight_Dir;
			if (lightDir.LengthSquared() > 0.0f)
			{
				lightDir.Normalize();

				const Math::Vector3 camPos = spCamera->GetMatrix().Translation();
				const Math::Vector3 sunWorldPos = camPos - lightDir * kSunDistance;
				spKdCamera->ConvertWorldToScreenDetail(sunWorldPos, m_sunScreenPos);

				Math::Viewport vp;
				KdDirect3D::Instance().CopyViewportInfo(vp);

				const float halfW = vp.width * 0.5f;
				const float halfH = vp.height * 0.5f;

				const float rateX = (halfW > 0.0f) ? std::abs(m_sunScreenPos.x) / halfW : 1.0f;
				const float rateY = (halfH > 0.0f) ? std::abs(m_sunScreenPos.y) / halfH : 1.0f;
				const float edgeRate = std::max(rateX, rateY);
				// 光源が画面端へ近づくほど、フレアを自然に減衰させる。
				const float edgeFade = std::clamp(1.0f - std::max(edgeRate - 0.85f, 0.0f) / 0.35f, 0.0f, 1.0f);

				if (m_sunScreenPos.z > 0.0f && edgeFade > 0.0f)
				{
					// 画面内にあり、かつ遮蔽されていない時だけ強度を持たせる。
					targetIntensity = edgeFade * CalcOcclusionRate(camPos, sunWorldPos, spCamera.get());
				}
			}
		}
	}

	// 描画強度は徐々に追従させ、点滅のような見え方を避ける。
	m_intensity += (targetIntensity - m_intensity) * kFadeSpeed;
}

// ブライトパスでは中心の強い発光だけを加算合成する。
void LensFlare::DrawBright()
{
	if (!m_spTexture) { return; }
	if (m_intensity <= 0.01f) { return; }

	KdShaderManager::Instance().m_spriteShader.Begin(true);
	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);
	DrawFlare(true);
	KdShaderManager::Instance().UndoBlendState();
	KdShaderManager::Instance().m_spriteShader.End();
}

// 通常パスではフレア本体に加えてゴーストも描画する。
void LensFlare::DrawSprite()
{
	if (!m_spTexture) { return; }
	if (m_intensity <= 0.01f) { return; }

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);
	DrawFlare(false);
	KdShaderManager::Instance().UndoBlendState();
}

// 呼び出し元で使っているカメラを受け取り、以後の計算へ使う。
void LensFlare::SetCamera(const std::shared_ptr<CameraBase>& camera)
{
	if (!camera) { return; }

	m_wpCamera = camera;
}

// 光源方向への視線上に遮蔽物があるかを調べ、表示可否を 0 / 1 で返す。
float LensFlare::CalcOcclusionRate(const Math::Vector3& camPos, const Math::Vector3& sunWorldPos, const CameraBase* camera) const
{
	KdCollider::RayInfo rayInfo;
	rayInfo.m_pos = camPos;
	rayInfo.m_dir = sunWorldPos - camPos;
	rayInfo.m_range = rayInfo.m_dir.Length();
	if (rayInfo.m_range <= 0.0f) { return 0.0f; }

	rayInfo.m_dir.Normalize();
	rayInfo.m_type = KdCollider::TypeGround | KdCollider::TypeBump;

	float nearestOverlap = -1.0f;
	bool isHit = false;

	// シーン上のコライダを総当たりし、光源までのレイを遮るものがあるか確認する。
	for (const std::shared_ptr<KdGameObject>& obj : SceneManager::Instance().GetObjList())
	{
		if (!obj) { continue; }
		if (obj.get() == this) { continue; }
		if (obj.get() == camera) { continue; }

		std::list<KdCollider::CollisionResult> hitList;
		if (!obj->Intersects(rayInfo, &hitList)) { continue; }

		for (const KdCollider::CollisionResult& hit : hitList)
		{
			if (!isHit || hit.m_overlapDistance > nearestOverlap)
			{
				nearestOverlap = hit.m_overlapDistance;
				isHit = true;
			}
		}
	}

	return isHit ? 0.0f : 1.0f;
}

// ディスク・リング・ストリーク・ゴーストを並べて、レンズフレアらしい見た目を作る。
void LensFlare::DrawFlare(bool brightPass)
{
	// テクスチャは 2x2 に分割されており、それぞれ別のフレア素材になっている。
	const Math::Rectangle discRect = GetSrcRect(0, 0);
	const Math::Rectangle ringRect = GetSrcRect(1, 0);
	const Math::Rectangle streakRect = GetSrcRect(0, 1);
	const Math::Rectangle ghostRect = GetSrcRect(1, 1);

	const Math::Vector2 sunPos = { m_sunScreenPos.x, m_sunScreenPos.y };
	const Math::Vector2 toCenter = -sunPos;
	const float scale = 0.9f + m_intensity * 0.35f;

	auto& spriteShader = KdShaderManager::Instance().m_spriteShader;

	const Math::Color discColor = { 1.0f, 0.95f, 0.82f, m_intensity * (brightPass ? 1.0f : 0.55f) };
	const Math::Color ringColor = { 0.80f, 0.90f, 1.0f, m_intensity * (brightPass ? 0.70f : 0.28f) };
	const Math::Color streakColor = { 1.0f, 0.90f, 0.75f, m_intensity * (brightPass ? 0.85f : 0.35f) };

	// 光源位置には中心の発光、リング、横方向の筋を重ねる。
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(220.0f * scale), (int)(220.0f * scale), &discRect, &discColor);
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(280.0f * scale), (int)(280.0f * scale), &ringRect, &ringColor);
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(520.0f * scale), (int)(110.0f * scale), &streakRect, &streakColor);

	if (brightPass) { return; }

	// 通常パスでは、画面中心へ向かうライン上にゴーストを並べる。
	const std::array<float, 4> factors = { 0.35f, 0.75f, 1.15f, 1.55f };
	const std::array<float, 4> sizes = { 90.0f, 60.0f, 120.0f, 70.0f };
	const std::array<Math::Color, 4> ghostColors =
	{
		Math::Color(0.85f, 0.95f, 1.0f, m_intensity * 0.24f),
		Math::Color(1.0f, 0.82f, 0.72f, m_intensity * 0.20f),
		Math::Color(0.70f, 0.85f, 1.0f, m_intensity * 0.22f),
		Math::Color(1.0f, 0.95f, 0.80f, m_intensity * 0.16f)
	};

	for (size_t i = 0; i < factors.size(); ++i)
	{
		const Math::Vector2 ghostPos = sunPos + toCenter * factors[i];
		const int drawSize = (int)(sizes[i] * scale);
		spriteShader.DrawTex(m_spTexture.get(), (int)ghostPos.x, (int)ghostPos.y, drawSize, drawSize, &ghostRect, &ghostColors[i]);
	}
}

// スプライトシート上のセルを描画用矩形へ変換する。
Math::Rectangle LensFlare::GetSrcRect(int xIndex, int yIndex) const
{
	Math::Rectangle rect = {};

	if (!m_spTexture) { return rect; }

	const int cellW = (int)m_spTexture->GetWidth() / 2;
	const int cellH = (int)m_spTexture->GetHeight() / 2;

	rect.x = cellW * xIndex;
	rect.y = cellH * yIndex;
	rect.width = cellW;
	rect.height = cellH;

	return rect;
}
