#pragma once

class CharacterBase : public KdGameObject
{
public:
	CharacterBase()									{}
	~CharacterBase()					override	{}

	// ↓これらは明示的に呼び出す必要がある
	void Init()							override;
	void Update()						override;

	// ↓これらは自動的に行われる
	void PostUpdate()					override;
	void DrawLit()						override;
	void GenerateDepthMapFromLight()	override;

	void RegistHitObject(const std::shared_ptr<KdGameObject>& object)
	{
		m_wpHitObjectList.push_back(object);
	}

private:
	std::vector<std::weak_ptr<KdGameObject>>	m_wpHitObjectList{};

protected:
	std::shared_ptr<KdModelWork>	m_spModel		= nullptr;
	std::shared_ptr<KdAnimator>		m_spAnimator	= nullptr;
	float							m_Gravity		= 0;
	Math::Vector3					m_worldPos		= Math::Vector3::Zero;
	Math::Vector3					m_worldRot		= Math::Vector3::Zero;

	

	// 回転行列の生成
	void UpdateRotate(const Math::Vector3& srcMoveVec);
	// 行列の更新
	void UpdateMatrix();
	// 衝突判定とそれに伴う座標の更新
	void UpdateCollision();
	// アニメーションの更新
	void UpdateAnimation();

// デバッグ用変数
protected:
	// ①当たり判定(球判定)用の情報
	std::shared_ptr<DirectX::BoundingSphere> m_spSphere;
	bool m_IsPlayer = false;
};