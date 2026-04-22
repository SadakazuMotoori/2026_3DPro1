#include "LensFlare.h"

#include "../../../../../Scene/SceneManager.h"
#include "../../../../Camera/CameraBase.h"

namespace
{
	// カメラ前方の少し先を仮想的な太陽位置として扱う距離。
	constexpr float kSunDistance = 400.0f;
	// 明るさを急変させず、自然に追従させる補間係数。
	constexpr float kFadeSpeed = 0.18f;
	// フレア素材シートの分割数。
	constexpr int kSheetColumns = 3;
	constexpr int kSheetRows = 2;
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

// 通常パスではフレア本体に加えてゴーストも描画する。
void LensFlare::DrawSprite()
{
	if (!m_spTexture) { return; }
	if (m_intensity <= 0.01f) { return; }

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Add);
	DrawFlare();
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
void LensFlare::DrawFlare()
{
	// テクスチャは 3x2 に分割されており、それぞれ別のフレア素材になっている。
	const Math::Rectangle ringRect = GetSrcRect(1, 0);
	const Math::Rectangle discRect = GetSrcRect(2, 0);
	const Math::Rectangle ghostRect = GetSrcRect(1, 1);
	const Math::Rectangle hexRect = GetSrcRect(2, 1);

	const Math::Vector2 sunPos = { m_sunScreenPos.x, m_sunScreenPos.y };
	const Math::Vector2 toCenter = -sunPos;
	const float sourceScale = 0.9f + m_intensity * 0.4f;
	const float ghostScale = 0.9f + m_intensity * 0.3f;

	auto& spriteShader = KdShaderManager::Instance().m_spriteShader;

	const Math::Color ringColor = { 1.0f, 0.42f, 0.28f, m_intensity * 0.24f };
	const Math::Color outerRingColor = { 0.82f, 0.36f, 0.30f, m_intensity * 0.10f };

	// 通常パスでは、大きい暖色リングと複数のゴーストを画面中心方向へ並べる。
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(600.0f * sourceScale), (int)(600.0f * sourceScale), &ringRect, &ringColor);
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(860.0f * sourceScale), (int)(860.0f * sourceScale), &ringRect, &outerRingColor);

	const std::array<float, 5> factors = { 0.30f, 0.52f, 0.88f, 1.22f, 1.58f };
	const std::array<float, 5> sizes = { 96.0f, 28.0f, 92.0f, 150.0f, 238.0f };
	const std::array<Math::Rectangle, 5> ghostRects =
	{
		discRect,
		discRect,
		hexRect,
		ghostRect,
		ghostRect
	};
	const std::array<Math::Color, 5> ghostColors =
	{
		Math::Color(0.84f, 0.90f, 1.0f, m_intensity * 0.18f),
		Math::Color(0.62f, 1.0f, 0.78f, m_intensity * 0.28f),
		Math::Color(1.0f, 0.66f, 0.38f, m_intensity * 0.17f),
		Math::Color(0.92f, 0.46f, 0.74f, m_intensity * 0.18f),
		Math::Color(0.34f, 0.30f, 1.0f, m_intensity * 0.26f)
	};

	for (size_t i = 0; i < factors.size(); ++i)
	{
		const Math::Vector2 ghostPos = sunPos + toCenter * factors[i];
		const int drawSize = (int)(sizes[i] * ghostScale);
		spriteShader.DrawTex(m_spTexture.get(), (int)ghostPos.x, (int)ghostPos.y, drawSize, drawSize, &ghostRects[i], &ghostColors[i]);
	}
}

// スプライトシート上のセルを描画用矩形へ変換する。
Math::Rectangle LensFlare::GetSrcRect(int xIndex, int yIndex) const
{
	Math::Rectangle rect = {};

	if (!m_spTexture) { return rect; }

	const int cellW = (int)m_spTexture->GetWidth() / kSheetColumns;
	const int cellH = (int)m_spTexture->GetHeight() / kSheetRows;

	rect.x = cellW * xIndex;
	rect.y = cellH * yIndex;
	rect.width = cellW;
	rect.height = cellH;

	return rect;
}
