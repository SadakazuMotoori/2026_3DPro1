#include "Enemy01.h"

void Enemy01::Init()
{
	CharacterBase::Init();

	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelWork>();
		m_spModel->SetModelData("Asset/Models/Character/Robot/Robot.gltf");

		m_pCollider = std::make_unique<KdCollider>();
		KdCollider::CapsuleInfo _cpInfo(KdCollider::TypeBump, GetPos(), {0.0f, 0.7f, 0.0f}, 1.5f, 0.3f);
		m_pCollider->RegisterCollisionShape("Enemy", _cpInfo);

	//	DirectX::BoundingSphere _info({ 0.0f, 1.0f, 0.0f }, 0.5f);
	//	m_pCollider->RegisterCollisionShape("Enemy", _info, KdCollider::TypeBump);
	}

	SetPos({ 2, 0, 5 });
}

void Enemy01::Update()
{
	m_Gravity += 0.01f;
	m_mWorld._42 -= m_Gravity;

	m_worldPos = GetPos();

	CharacterBase::Update();
}

void Enemy01::PreDraw()
{
//	m_pDebugWire->AddDebugSphere(GetMatrix().Translation() + Math::Vector3(0.0f, 1.0f, 0.0f), 0.5f);
	m_pDebugWire->AddDebugCapsule(GetMatrix(), { 0,0.7f,0 }, 1.5f, 0.3f);
}
