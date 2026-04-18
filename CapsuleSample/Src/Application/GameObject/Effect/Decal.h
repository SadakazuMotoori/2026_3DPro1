#pragma once

class Decal : public KdGameObject
{
public:
	Decal()						{}
	~Decal()			override	{}

	void Init()			override;
	void Update()		override;
	void PreDraw()		override;

	void SetPos(const Math::Vector3& pos) override;
	void SetScale(const Math::Vector3& scale) override;
	void SetRotation(const Math::Vector3& rotation);

	void SetColor(const Math::Color& color) { m_color = color; }
	void SetNormalThreshold(float threshold) { m_normalThreshold = std::clamp(threshold, 0.0f, 1.0f); }
	void SetTexture(const std::shared_ptr<KdTexture>& spTexture);
	void SetTarget(const std::shared_ptr<KdGameObject>& target)
	{
		if (!target) { return; }

		m_wpTarget = target;
	}

private:
	void UpdateMatrix();

	Math::Vector3				m_position = Math::Vector3::Zero;
	Math::Vector3				m_scale = {5,5,5};
	Math::Vector3				m_rotation = Math::Vector3::Zero;
	Math::Color					m_color = { 0.55f, 0.08f, 0.08f, 0.75f };
	float						m_normalThreshold = 0.8f;
	std::shared_ptr<KdTexture>	m_spTexture = nullptr;

	std::weak_ptr<KdGameObject>	m_wpTarget;
};
