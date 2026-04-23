#include "Decal.h"

void Decal::Init()
{
	if (!m_spTexture)
	{
		m_spTexture = std::make_shared<KdTexture>("Asset/Textures/Decal/decal_tactical_mark_01.png");
	}

	UpdateMatrix();
}

void Decal::Update()
{
	if (m_wpTarget.expired()) return;

	SetPos(m_position = m_wpTarget.lock()->GetPos());
}

void Decal::PreDraw()
{
	if (!m_spTexture) { return; }

	if (m_wpTarget.expired()) return;
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
	const Math::Matrix scaleMat = Math::Matrix::CreateScale(m_scale);
	const Math::Matrix rotationMat = Math::Matrix::CreateFromYawPitchRoll(
		DirectX::XMConvertToRadians(m_rotation.y),
		DirectX::XMConvertToRadians(m_rotation.x),
		DirectX::XMConvertToRadians(m_rotation.z));
	const Math::Matrix translateMat = Math::Matrix::CreateTranslation(m_position);

	m_mWorld = scaleMat * rotationMat * translateMat;
}
