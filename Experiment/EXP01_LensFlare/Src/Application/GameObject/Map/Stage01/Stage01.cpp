#include "Stage01.h"

void Stage01::Init()
{
	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelData>();
		m_spModel->Load("Asset/Models/Stage/StageMap.gltf");

		// HITオブジェクトとしての登録
		m_pCollider = std::make_unique<KdCollider>();
		m_pCollider->RegisterCollisionShape("Stage01", m_spModel, KdCollider::TypeGround | KdCollider::TypeBump);
	}
}