#include "Enemy01.h"

namespace
{
	constexpr float kEnemyCapsuleRadius = 0.3f;
	constexpr float kEnemyCapsuleHeight = 1.5f;
	const Math::Vector3 kEnemyCapsuleOffset = { 0.0f, 0.7f, 0.0f };
}

void Enemy01::Init()
{
	CharacterBase::Init();

	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelWork>();
		m_spModel->SetModelData("Asset/Models/Character/Robot/Robot.gltf");

//		m_pCollider = std::make_unique<KdCollider>();
//		m_pCollider->RegisterCollisionShape("Enemy", kEnemyCapsuleOffset, kEnemyCapsuleRadius, kEnemyCapsuleHeight, KdCollider::TypeBump);
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
	m_pDebugWire->AddDebugCapsule(GetMatrix(), 0.3f, 1.5f, { 0,0.7f,0 });
}
