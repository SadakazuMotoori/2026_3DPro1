#pragma once
#include "../CharacterBase.h"

class CameraBase;
class PlayerBase : public CharacterBase
{
public:
	PlayerBase()				{}
	~PlayerBase()	override	{}

	void SetCamera(const std::shared_ptr<CameraBase>& camera)
	{
		m_wpCamera = camera;
	}

	const std::weak_ptr<CameraBase> GetCamera() { return m_wpCamera; }

private:
	std::weak_ptr<CameraBase> m_wpCamera;
};
