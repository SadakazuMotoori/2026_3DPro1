#include "Mirror.h"

void Mirror::Init()
{
	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelData>();
		m_spModel->Load("Asset/Models/Stage/Mirror/Mirror.gltf");
	}

	SetPos({0,0.1f,0});
}

void Mirror::DrawLit()
{
	if (!m_spModel) return;

	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_mWorld);
}