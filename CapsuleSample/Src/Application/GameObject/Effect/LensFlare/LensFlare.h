#pragma once

class CameraBase;

class LensFlare : public KdGameObject
{
public:
	LensFlare()						{}
	~LensFlare()		override	{}

	void Init()			override;
	void PostUpdate()	override;
	void DrawBright()	override;
	void DrawSprite()	override;

	void SetCamera(const std::shared_ptr<CameraBase>& camera);

private:
	float CalcOcclusionRate(const Math::Vector3& camPos, const Math::Vector3& sunWorldPos, const CameraBase* camera) const;
	void DrawFlare(bool brightPass);
	Math::Rectangle GetSrcRect(int xIndex, int yIndex) const;

	std::weak_ptr<CameraBase>	m_wpCamera;
	std::shared_ptr<KdTexture>	m_spTexture = nullptr;

	Math::Vector3				m_sunScreenPos = Math::Vector3::Zero;
	float						m_intensity = 0.0f;
};
