#pragma once
#include "../../MapBase.h"

class Mirror : public MapBase
{
public:
	Mirror()						{}
	~Mirror()			override	{}

	void Init()			override;
	void DrawLit()		override;

private:
	void CalcMirrorPlaneInfo();
	bool CalcSubsetBounds(const KdMesh& mesh, const KdMeshSubset& subset, Math::Vector3& center, Math::Vector3& extents) const;

	// 鏡面情報はモデル読込時に一度だけ計算し、描画時はワールド変換だけで使い回す。
	Math::Vector3 m_planeCenterModel = Math::Vector3::Zero;
	Math::Vector3 m_planeNormalModel = Math::Vector3::Backward;
	Math::Vector3 m_planeRightModel = Math::Vector3::Right;
	Math::Vector3 m_planeUpModel = Math::Vector3::Up;

	float m_planeHalfWidthModel = 0.5f;
	float m_planeHalfHeightModel = 0.5f;

	float m_reflectionStrength = 0.0f;
	float m_reflectionRoughness = 1.0f;

	bool m_hasMirrorSurface = false;

	static constexpr float kMirrorMetallicThreshold = 0.5f;
	static constexpr float kMaxReflectionStrength = 0.95f;
};
