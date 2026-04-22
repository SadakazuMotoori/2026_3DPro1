#include "LensFlare.h"

/*
	このファイルは、「太陽そのものを描く」のではなく、
	平行光源の方向を手掛かりにしてレンズフレアだけを画面へ重ねる処理をまとめたものです。

	初心者向けのざっくりした流れ:
	1. PostUpdate で「太陽が画面のどこに見える想定か」を計算する。
	2. その位置がカメラ前方にあるか、画面端に近すぎないかを調べる。
	3. カメラから太陽方向へレイを飛ばし、途中に遮蔽物がないかを調べる。
	4. その結果からフレアの明るさ m_intensity を少しずつ更新する。
	5. DrawSprite で、リングやゴーストを加算合成で描く。

	この実装で大事な考え方:
	- 平行光源は「位置」を持たないため、カメラの前方に仮の太陽位置を作って扱います。
	- フレアは本物の太陽画像ではなく、スクリーン上の装飾エフェクトです。
	- 明るさは毎フレームいきなり切り替えず、補間して自然に見えるようにしています。
	- ゴーストは「光源位置から画面中心へ向かう線」の上に並べることで、
	  それらしいレンズフレアの見た目を作っています。
*/
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
	constexpr int kSheetRows = 1;
}

// フレア描画に必要なテクスチャを一度だけ読み込む。
void LensFlare::Init()
{
	if (!m_spTexture)
	{
		// レンズフレア専用の素材シートを読み込む。
		m_spTexture = std::make_shared<KdTexture>("Asset/Textures/LensFlare/flare_sheet.png");
	}
}

// ライト方向をスクリーン座標へ変換し、表示可能かどうかと明るさを更新する。
void LensFlare::PostUpdate()
{
	// このフレームで目標にしたい明るさ。
	// 最後に m_intensity へなめらかに追従させるため、まずは別変数で計算する。
	float targetIntensity = 0.0f;

	const std::shared_ptr<CameraBase> spCamera = m_wpCamera.lock();
	if (spCamera && m_spTexture)
	{
		const std::shared_ptr<KdCamera>& spKdCamera = spCamera->GetCamera();
		if (spKdCamera)
		{
			// ワールド座標 -> スクリーン座標変換に使うため、
			// 現在のカメラ行列を KdCamera 側へ反映しておく。
			spKdCamera->SetCameraMatrix(spCamera->GetMatrix());

			// 平行光源の逆方向にある位置を、画面上のフレア発生源として使う。
			Math::Vector3 lightDir = KdShaderManager::Instance().GetLightCB().DirLight_Dir;
			if (lightDir.LengthSquared() > 0.0f)
			{
				// 「向き」だけを使いたいので長さを 1 にそろえる。
				// これで kSunDistance をそのまま距離として扱える。
				lightDir.Normalize();

				// 平行光源には実際の位置がないため、
				// カメラから光の逆方向へ一定距離だけ進めた点を
				// 仮の太陽位置として扱う。
				const Math::Vector3 camPos = spCamera->GetMatrix().Translation();
				const Math::Vector3 sunWorldPos = camPos - lightDir * kSunDistance;
				// 仮の太陽位置をスクリーン座標へ変換する。
				// x, y は画面中心からのずれ量、z はカメラ前方かどうかの判定に使う。
				spKdCamera->ConvertWorldToScreenDetail(sunWorldPos, m_sunScreenPos);

				Math::Viewport vp;
				KdDirect3D::Instance().CopyViewportInfo(vp);

				// 画面中心から左右端・上下端までの距離。
				// 以降の「どれだけ画面端に寄っているか」の正規化に使う。
				const float halfW = vp.width * 0.5f;
				const float halfH = vp.height * 0.5f;

				// 画面中心からのずれを、画面半分の長さで割って正規化する。
				// 1.0 付近ならその軸方向の画面端に近い。
				const float rateX = (halfW > 0.0f) ? std::abs(m_sunScreenPos.x) / halfW : 1.0f;
				const float rateY = (halfH > 0.0f) ? std::abs(m_sunScreenPos.y) / halfH : 1.0f;
				// x, y のうち、より端に近い方を採用する。
				const float edgeRate = std::max(rateX, rateY);
				// 光源が画面端へ近づくほど、フレアを自然に減衰させる。
				// edgeRate が 0.85 までは最大、そこから先は徐々に減衰し、
				// 1.20 付近で完全に見えなくなるようにしている。
				const float edgeFade = std::clamp(1.0f - std::max(edgeRate - 0.85f, 0.0f) / 0.35f, 0.0f, 1.0f);

				if (m_sunScreenPos.z > 0.0f && edgeFade > 0.0f)
				{
					// 画面内にあり、かつ遮蔽されていない時だけ強度を持たせる。
					// edgeFade は「画面端による減衰」、
					// CalcOcclusionRate は「遮蔽されているかどうか」を返す。
					// 両方を掛け合わせて、このフレームの目標明るさを作る。
					targetIntensity = edgeFade * CalcOcclusionRate(camPos, sunWorldPos, spCamera.get());
				}
			}
		}
	}

	// 描画強度は徐々に追従させ、点滅のような見え方を避ける。
	// 差分の一部だけを毎フレーム足すことで、0 / 1 の急変を防いでいる。
	m_intensity += (targetIntensity - m_intensity) * kFadeSpeed;
}

// 通常パスではフレア本体に加えてゴーストも描画する。
void LensFlare::DrawSprite()
{
	// テクスチャ未読み込みなら描画できない。
	if (!m_spTexture) { return; }
	// ほぼ見えない強度なら描画を省略する。
	if (m_intensity <= 0.01f) { return; }

	// 光の重なり表現なので、通常のアルファ合成ではなく加算合成で描く。
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
	// レイの始点はカメラ位置。
	rayInfo.m_pos = camPos;
	// カメラから仮の太陽位置へ向かう方向ベクトル。
	rayInfo.m_dir = sunWorldPos - camPos;
	// レイの長さは仮の太陽位置までに限定する。
	rayInfo.m_range = rayInfo.m_dir.Length();
	if (rayInfo.m_range <= 0.0f) { return 0.0f; }

	// 当たり判定では方向と距離を分けて扱うため、方向は正規化する。
	rayInfo.m_dir.Normalize();
	// 地形や障害物だけを遮蔽物として扱う。
	rayInfo.m_type = KdCollider::TypeGround | KdCollider::TypeBump;

	float nearestOverlap = -1.0f;
	bool isHit = false;

	// シーン上のコライダを総当たりし、光源までのレイを遮るものがあるか確認する。
	for (const std::shared_ptr<KdGameObject>& obj : SceneManager::Instance().GetObjList())
	{
		if (!obj) { continue; }
		// 自分自身は常にレイの始点付近にいるため、遮蔽物に含めない。
		if (obj.get() == this) { continue; }
		// カメラ自身も除外する。
		if (obj.get() == camera) { continue; }

		std::list<KdCollider::CollisionResult> hitList;
		// 交差していないオブジェクトは無視する。
		if (!obj->Intersects(rayInfo, &hitList)) { continue; }

		for (const KdCollider::CollisionResult& hit : hitList)
		{
			// 複数ヒットした時に備えて、代表値として最も大きい overlapDistance を保持する。
			if (!isHit || hit.m_overlapDistance > nearestOverlap)
			{
				nearestOverlap = hit.m_overlapDistance;
				isHit = true;
			}
		}
	}

	// 今回欲しいのは「遮蔽物があるかどうか」のみ。
	// 何か 1 つでも当たれば 0、何もなければ 1 を返す。
	return isHit ? 0.0f : 1.0f;
}

// ディスク・リング・ストリーク・ゴーストを並べて、レンズフレアらしい見た目を作る。
void LensFlare::DrawFlare()
{
	// テクスチャは 3x2 に分割されており、それぞれ別のフレア素材になっている。
	// 現在使用しているシートは 3 列 1 行で、左から ring / ghost / hex の並び。
	const Math::Rectangle ringRect = GetSrcRect(0, 0);
	const Math::Rectangle ghostRect = GetSrcRect(1, 0);
	const Math::Rectangle hexRect = GetSrcRect(2, 0);

	// 仮の太陽位置をそのままフレアの基準位置として使う。
	const Math::Vector2 sunPos = { m_sunScreenPos.x, m_sunScreenPos.y };
	// 光源位置から画面中心へ向かうベクトル。
	// ゴーストをこの線上へ並べることで、レンズフレアらしい配置になる。
	const Math::Vector2 toCenter = -sunPos;
	// 発生源のリング群の拡大率。明るいほど少しだけ大きくする。
	const float sourceScale = 0.9f + m_intensity * 0.4f;
	// ゴースト側の拡大率。こちらは少し控えめに変化させる。
	const float ghostScale = 0.9f + m_intensity * 0.3f;

	auto& spriteShader = KdShaderManager::Instance().m_spriteShader;

	// RGB は色味、A は透明度。
	// A に m_intensity を掛けることで、全体の見え方をまとめて制御する。
	const Math::Color ringColor = { 1.0f, 0.42f, 0.28f, m_intensity * 0.24f };
	const Math::Color outerRingColor = { 0.82f, 0.36f, 0.30f, m_intensity * 0.10f };

	// 通常パスでは、大きい暖色リングと複数のゴーストを画面中心方向へ並べる。
	// まずは光源位置にリングを重ねて、フレアの発生源らしさを作る。
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(600.0f * sourceScale), (int)(600.0f * sourceScale), &ringRect, &ringColor);
	spriteShader.DrawTex(m_spTexture.get(), (int)sunPos.x, (int)sunPos.y, (int)(860.0f * sourceScale), (int)(860.0f * sourceScale), &ringRect, &outerRingColor);

	// 0.0 なら光源位置、1.0 なら画面中心、
	// 1.0 を超えると画面中心を越えた反対側へ配置される。
	const std::array<float, 5> factors = { 0.30f, 0.52f, 0.88f, 1.22f, 1.58f };
	// 各ゴーストの描画サイズ。
	const std::array<float, 5> sizes = { 96.0f, 28.0f, 92.0f, 150.0f, 238.0f };
	// 各ゴーストで使う素材を定義する。
	const std::array<Math::Rectangle, 5> ghostRects =
	{
		ghostRect,
		ghostRect,
		hexRect,
		ghostRect,
		hexRect
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
		// 光源位置から画面中心へ向かう線上を、factor の割合だけ進めた位置。
		// factor > 1.0 の要素は中心を通り越して反対側へ出る。
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

	// シート全体を分割数で割って、1 セルの大きさを求める。
	const int cellW = (int)m_spTexture->GetWidth() / kSheetColumns;
	const int cellH = (int)m_spTexture->GetHeight() / kSheetRows;

	// xIndex, yIndex で指定されたセル番号を、
	// 実際のピクセル座標の矩形へ変換する。
	rect.x = cellW * xIndex;
	rect.y = cellH * yIndex;
	rect.width = cellW;
	rect.height = cellH;

	return rect;
}
