#include "DistortionObject.h"

//============================================================
//
// DistortionObject の役割
//
// このクラスは、「背景そのものを直接描き替える」のではなく、
// 「ここに歪みを発生させたい」という情報だけを 3D 空間へ置くためのオブジェクトです。
// そのため、通常の色付きエフェクトとは少し処理の流れが異なります。
//
// 今回の全体の流れは次のようになっています。
//
// 1. このクラスがワールド座標に板ポリを置く
//    まずは「どこで歪みを発生させるか」を 3D 空間上に配置します。
//    ここで置いているのは見た目用の板ポリですが、まだ画面には普通の色として出しません。
//
// 2. DrawDistortion で歪み専用 RT へ情報を書き出す
//    通常のカラー RT ではなく、歪み専用 RT に対して
//    「UV をどちらへずらすか」というベクトル情報だけを書き込みます。
//    つまり、この段階では“絵”ではなく“曲げ方のメモ”を描いています。
//
// 3. 歪み方は StandardShader 側の Distortion 用 PS が決める
//    HeatHaze ならノイズを使って面の中をゆらがせ、
//    ShockWave なら中心から外側へ押し出す輪の形を作ります。
//    DistortionObject 側は、そのために必要なパラメータを渡す役目です。
//
// 4. PostProcessShader の最後で背景へ適用する
//    ポストエフェクトの最終段で、歪み RT に入っているベクトルを使って
//    背景画像の UV をずらし直す事で、初めて「背景が歪んで見える」状態になります。
//
// 5. この方式の利点
//    歪みの発生源は 3D 空間に置ける一方で、実際に曲げるのは背景画像なので、
//    UI や後段の 2D 表示まで一緒に歪ませずに済みます。
//    そのため、A/B、炎、HIT の衝撃波などへ横展開しやすい作りになっています。
//
// 要するに、このクラスは「歪みそのもの」ではなく、
// 「歪みをどこでどう発生させるか」をワールドに置くための部品です。
//
//============================================================
void DistortionObject::Init()
{
	// ノイズテクスチャを 1 枚だけ設定しておく。
	// 熱気ではこのノイズをそのまま使い、
	// 衝撃波ではテクスチャ形状よりもシェーダー内計算を主に使う。
	m_quad.SetMaterial("Asset/Textures/System/WhiteNoise.png");

	// この板ポリは 2D 用ではなく、3D 空間に置いたビルボードとして扱う。
	m_quad.Set2DObject(false);
}

void DistortionObject::Update()
{
	// 時間は毎フレーム一定量だけ進める。
	// 今回はまず見た目確認が目的なので、固定フレーム前提の簡易進行にしている。
	constexpr float kFrameTime = 1.0f / 60.0f;
	m_time += kFrameTime;

	if (m_type == Type::ShockWave)
	{
		// 衝撃波は、現在の半径を毎フレーム少しずつ広げていく。
		// シェーダー側はこの半径を見て、輪状の歪み位置を決める。
		m_ringRadius += m_ringSpeed;

		if (m_ringRadius > m_ringMaxRadius)
		{
			if (m_loop)
			{
				// ループ指定なら、最大半径到達後に最初から再生し直す。
				m_ringRadius = 0.0f;
			}
			else
			{
				// 一度きりの衝撃波として使う場合は寿命切れにする。
				m_isExpired = true;
			}
		}
	}
}

void DistortionObject::DrawDistortion()
{
	// 現在の種類と各種パラメータを、歪み用シェーダーへ渡す。
	ApplyDistortionParameter();

	// カメラ正対の板ポリを描き、歪み発生範囲をワールド空間へ置く。
	// ここで出るのは色ではなく、背景を曲げるためのベクトル情報だけ。
	KdShaderManager::Instance().m_StandardShader.DrawPolygon(m_quad, CreateBillboardMatrix());
}

Math::Matrix DistortionObject::CreateBillboardMatrix() const
{
	// カメラのビュー行列を反転すると、「カメラから見た向きの逆」が得られる。
	// これを使うと、板ポリを常にカメラ正面へ向けられる。
	Math::Matrix billboard = KdShaderManager::Instance().GetCameraCB().mView.Invert();

	// 位置成分まで残すとカメラ移動に引っ張られるため、回転だけを使う。
	billboard.Translation(Math::Vector3::Zero);

	// 最後に大きさとワールド座標を掛け合わせて完成。
	Math::Matrix scale = Math::Matrix::CreateScale(m_size.x, m_size.y, 1.0f);
	Math::Matrix trans = Math::Matrix::CreateTranslation(GetPos());

	return scale * billboard * trans;
}

void DistortionObject::ApplyDistortionParameter() const
{
	auto& shader = KdShaderManager::Instance().m_StandardShader;

	// 種類に関係なく使う基本値を先に設定する。
	// Time はアニメーション進行用、Strength は最終的な歪み量の強さ。
	shader.SetDistortionTime(m_time);
	shader.SetDistortionStrength(m_strength);

	if (m_type == Type::HeatHaze)
	{
		// 熱気・蜃気楼ではノイズの読み方を設定する。
		// Param0.zw : ノイズを何回繰り返すか
		// Param1.xy : ノイズをどの向きへ流すか
		shader.SetDistortionType(KdStandardShader::DistortionType::HeatHaze);
		shader.SetDistortionParam0({ 0.0f, 0.0f, m_noiseTiling.x, m_noiseTiling.y });
		shader.SetDistortionParam1({ m_scrollSpeed.x, m_scrollSpeed.y, 0.0f, 0.0f });
	}
	else
	{
		// 衝撃波では、現在半径と輪の太さを渡す。
		// Param0.x : 現在の輪の半径
		// Param0.y : 輪の太さ
		shader.SetDistortionType(KdStandardShader::DistortionType::ShockWave);
		shader.SetDistortionParam0({ m_ringRadius, m_ringThickness, 1.0f, 1.0f });
		shader.SetDistortionParam1({ 0.0f, 0.0f, 0.0f, 0.0f });
	}
}
