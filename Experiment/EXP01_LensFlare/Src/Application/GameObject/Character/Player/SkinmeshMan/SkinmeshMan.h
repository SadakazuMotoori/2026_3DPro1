#pragma once
#include "../PlayerBase.h"

class SkinmeshMan : public PlayerBase
{
public:
	SkinmeshMan()					{}
	~SkinmeshMan()		override	{}

	void Init()			override;
	void Update()		override;

	void PreDraw()		override;
	void DrawSprite()	override;
};
