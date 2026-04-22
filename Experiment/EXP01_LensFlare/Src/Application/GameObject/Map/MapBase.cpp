#include "MapBase.h"

void MapBase::Init()
{
}

void MapBase::Update()
{
}

void MapBase::DrawLit()
{
	if (!m_spModel) return;

	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_mWorld);
}
