#pragma once
#include "../../MapBase.h"

class Mirror : public MapBase
{
public:
	Mirror()						{}
	~Mirror()			override	{}

	void Init()			override;
	void DrawLit()		override;
};
