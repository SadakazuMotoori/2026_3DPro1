#pragma once
#include "../EnemyBase.h"

class Enemy01 : public EnemyBase
{
public:
	Enemy01()										{}
	~Enemy01()							override	{}

	void Init()							override;
	void PreUpdate()					override;
	void Update()						override;

	void PreDraw()						override;
	void DrawLit()						override;
	void GenerateDepthMapFromLight()	override;
private:
	std::vector<Math::Matrix>			m_InstanceWorlds;
	int m_Num = 1;
};
