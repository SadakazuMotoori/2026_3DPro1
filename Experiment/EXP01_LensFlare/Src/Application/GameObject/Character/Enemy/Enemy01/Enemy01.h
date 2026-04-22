#pragma once
#include "../EnemyBase.h"

class Enemy01 : public EnemyBase
{
public:
	Enemy01()						{}
	~Enemy01()			override	{}

	void Init()			override;
	void Update()		override;

	void PreDraw()		override;
private:
	
};
