/*
	このファイルのデカール処理は、「薄い板を実際に置いて描く」のではなく、
	「この位置・向き・大きさの範囲に模様を投影してほしい」という情報をシェーダーへ渡す実装です。

	流れは次の通りです。
	1. Decal オブジェクトが位置・回転・拡大率からワールド行列を作る
	2. PreDraw で、その行列・色・テクスチャ・法線しきい値をシェーダーへ登録する
	3. ピクセルシェーダー側で各ピクセルのワールド座標をデカールのローカル空間へ逆変換する
	4. ローカル空間内の箱の範囲に入っているか、面の向きが合っているかを判定する
	5. XZ 座標から UV を作り、Y 座標を「投影の厚み」として使いながらベースカラーへ合成する

	このため、このファイルで作るワールド行列の意味は次のようになります。
	- X / Z スケール : 床や地面に広がるデカールの見た目の大きさ
	- Y スケール     : 上下方向にどれだけ厚みを持って投影するか
	- 回転            : どの向きにデカールを貼るか
	- 平行移動        : デカールの中心位置
*/
#include "Decal.h"

void Decal::Init()
{
	if (!m_spTexture)
	{
		// テクスチャ未指定時は、デカール確認用の既定画像を使う。
		m_spTexture = std::make_shared<KdTexture>("Asset/Textures/Decal/decal_tactical_mark_01.png");
	}

	UpdateMatrix();
}

void Decal::Update()
{
	if (m_wpTarget.expired()) return;

	// 追従対象がある場合は、毎フレームその現在位置にデカール中心を合わせる。
	SetScale({5,1,5});
	SetPos(m_position = m_wpTarget.lock()->GetPos());
}

void Decal::PreDraw()
{
	if (!m_spTexture) { return; }

	if (m_wpTarget.expired()) return;

	// 実際にここで板を描画するのではなく、そのフレームで使うデカール情報をシェーダーへ登録する。
	SetPos(m_position = m_wpTarget.lock()->GetPos());
	KdShaderManager::Instance().m_StandardShader.AddDecal(m_mWorld, m_spTexture, m_color, m_normalThreshold);
}

void Decal::SetPos(const Math::Vector3& pos)
{
	m_position = pos;
	UpdateMatrix();
}

void Decal::SetScale(const Math::Vector3& scale)
{
	m_scale = scale;
	UpdateMatrix();
}

void Decal::SetRotation(const Math::Vector3& rotation)
{
	m_rotation = rotation;
	UpdateMatrix();
}

void Decal::SetTexture(const std::shared_ptr<KdTexture>& spTexture)
{
	if (!spTexture) { return; }

	m_spTexture = spTexture;
}

void Decal::UpdateMatrix()
{
	// デカールはシェーダー側で「原点中心・各軸 ±0.5 の箱」として判定する。
	// ここではその基準箱を、必要な大きさ・向き・位置へ変換するワールド行列を作っている。
	const Math::Matrix scaleMat = Math::Matrix::CreateScale(m_scale);
	// 回転値は度数法で保持しているため、行列作成時にラジアンへ変換する。
	const Math::Matrix rotationMat = Math::Matrix::CreateFromYawPitchRoll(
		DirectX::XMConvertToRadians(m_rotation.y),
		DirectX::XMConvertToRadians(m_rotation.x),
		DirectX::XMConvertToRadians(m_rotation.z));
	const Math::Matrix translateMat = Math::Matrix::CreateTranslation(m_position);

	// 基準箱を「拡大 → 回転 → 移動」の順に変換することで、
	// ローカル空間のデカール領域をワールド空間上の目的位置へ配置する。
	m_mWorld = scaleMat * rotationMat * translateMat;
}
