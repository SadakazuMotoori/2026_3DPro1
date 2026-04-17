#include "Enemy01.h"

void Enemy01::Init()
{
	CharacterBase::Init();

	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelWork>();
		m_spModel->SetModelData("Asset/Models/Character/Robot/Robot.gltf");

		m_pCollider = std::make_unique<KdCollider>();
		m_spAnimator = std::make_shared<KdAnimator>();
		m_spAnimator->SetAnimation(m_spModel->GetAnimation("Stand"));

		// モデル登録
		KdCollider::CapsuleInfo _cpInfo(KdCollider::TypeBump, GetPos(), {0.0f, 0.7f, 0.0f}, 1.5f, 0.3f);
		m_pCollider->RegisterCollisionShape("Enemy", m_spModel, KdCollider::TypeBump);
	 
		// カプセル登録
	//	KdCollider::CapsuleInfo _cpInfo(KdCollider::TypeBump, GetPos(), {0.0f, 0.7f, 0.0f}, 1.5f, 0.3f);
	//	m_pCollider->RegisterCollisionShape("Enemy", _cpInfo);

		// 球登録
	//	DirectX::BoundingSphere _info({ 0.0f, 1.0f, 0.0f }, 0.5f);
	//	m_pCollider->RegisterCollisionShape("Enemy", _info, KdCollider::TypeBump);

		// BOX(AABB)登録
	//	Math::Vector3 _center = GetPos() + Math::Vector3(0, 0.5f, 0);
	//	DirectX::BoundingBox _boxInfo(_center, { 0.5f, 0.5f, 0.5f });
	//	m_pCollider->RegisterCollisionShape("Enemy", _boxInfo, KdCollider::TypeBump);

		// BOX(OBB)登録
	//	Math::Vector3 _center = GetPos() + Math::Vector3(0, 0.5f, 0);
	//	Math::Quaternion _qat = Math::Quaternion::CreateFromRotationMatrix(Math::Matrix::CreateRotationY(DirectX::XMConvertToRadians(45)));
	//	DirectX::BoundingOrientedBox _boxInfo(_center, { 0.5f, 0.5f, 0.5f }, _qat);
	//	m_pCollider->RegisterCollisionShape("Enemy", _boxInfo, KdCollider::TypeBump);
	}

	SetPos({ 2, 0, 5 });
}

void Enemy01::PreUpdate()
{
	m_InstanceWorlds.clear();
}
void Enemy01::Update()
{
	m_Gravity += 0.01f;
	m_mWorld._42 -= m_Gravity;
//	m_mWorld._43 += 0.01f;
	m_worldPos = GetPos();

	CharacterBase::Update();
}

void Enemy01::PreDraw()
{
	// 球デバッグワイヤーの描画
//	m_pDebugWire->AddDebugSphere(GetMatrix().Translation() + Math::Vector3(0.0f, 1.0f, 0.0f), 0.5f);

 	// カプセルデバッグワイヤーの描画
//	m_pDebugWire->AddDebugCapsule(GetMatrix(), { 0,0.7f,0 }, 1.5f, 0.3f);

	// BOX(AABB)デバッグワイヤーの描画
//	m_pDebugWire->AddDebugBox(GetMatrix(), {0.5f,0.5f ,0.5f}, { 0.0f,0.5f ,0.0f });

	// BOX(OBB)デバッグワイヤーの描画
//	Math::Matrix _mat = Math::Matrix::CreateRotationY(DirectX::XMConvertToRadians(45)) * Math::Matrix::CreateTranslation(GetPos());
//	m_pDebugWire->AddDebugBox(_mat, {0.5f,0.5f ,0.5f}, { 0.0f,0.5f ,0.0f },true);
}

void Enemy01::DrawLit()
{
	if (!m_spModel) return;

	for (int i = 0; i < m_Num; i++)
	{
		Math::Matrix _mat = Math::Matrix::Identity;
		_mat._41 = -20 + (20 - (i % 10));
		_mat._43 = -20 + (i / 10) * 2.0f;
		m_InstanceWorlds.push_back(_mat);
	}
	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_mWorld);
//	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_InstanceWorlds);
}

void Enemy01::GenerateDepthMapFromLight()
{
	if (!m_spModel) return;

	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_mWorld);
}