#include "Mirror.h"

void Mirror::Init()
{
	if (!m_spModel)
	{
		m_spModel = std::make_shared<KdModelData>();
		m_spModel->Load("Asset/Models/Stage/Mirror/Mirror.gltf");
	}

	CalcMirrorPlaneInfo();

	SetPos({0,0.1f,0});
}

void Mirror::DrawLit()
{
	if (!m_spModel) return;

	if (m_hasMirrorSurface)
	{
		// 読込時に求めたモデル空間の鏡面情報を、今のワールド行列へ変換して postprocess へ渡す。
		Math::Vector3 planeCenter = Math::Vector3::Transform(m_planeCenterModel, m_mWorld);

		Math::Vector3 planeNormal = Math::Vector3::TransformNormal(m_planeNormalModel, m_mWorld);
		if (planeNormal.LengthSquared() > 0.0f)
		{
			planeNormal.Normalize();
		}

		Math::Vector3 planeRight = Math::Vector3::TransformNormal(m_planeRightModel, m_mWorld);
		float halfWidth = planeRight.Length() * m_planeHalfWidthModel;
		if (planeRight.LengthSquared() > 0.0f)
		{
			planeRight.Normalize();
		}

		Math::Vector3 planeUp = Math::Vector3::TransformNormal(m_planeUpModel, m_mWorld);
		float halfHeight = planeUp.Length() * m_planeHalfHeightModel;
		if (planeUp.LengthSquared() > 0.0f)
		{
			planeUp.Normalize();
		}

		KdShaderManager::Instance().m_postProcessShader.AddSSPRPlane(
			planeCenter, planeNormal, planeRight, planeUp,
			halfWidth, halfHeight, m_reflectionStrength, m_reflectionRoughness);
	}

	KdShaderManager::Instance().m_StandardShader.DrawModel(*m_spModel, m_mWorld);
}

void Mirror::CalcMirrorPlaneInfo()
{
	m_hasMirrorSurface = false;
	m_reflectionStrength = 0.0f;
	m_reflectionRoughness = 1.0f;

	if (!m_spModel) return;

	const auto& dataNodes = m_spModel->GetOriginalNodes();
	const auto& drawNodeIndices = m_spModel->GetDrawMeshNodeIndices();
	const auto& materials = m_spModel->GetMaterials();

	float bestMetallic = -1.0f;
	float bestRoughness = 1.0f;

	for (int nodeIdx : drawNodeIndices)
	{
		if (nodeIdx < 0 || nodeIdx >= static_cast<int>(dataNodes.size())) continue;

		const auto& node = dataNodes[nodeIdx];
		if (!node.m_spMesh) continue;

		const auto& subsets = node.m_spMesh->GetSubsets();
		for (const auto& subset : subsets)
		{
			if (subset.MaterialNo >= materials.size()) continue;

			const KdMaterial& material = materials[subset.MaterialNo];
			if (material.m_metallicRate < kMirrorMetallicThreshold) continue;

			Math::Vector3 center = Math::Vector3::Zero;
			Math::Vector3 extents = Math::Vector3::Zero;
			if (!CalcSubsetBounds(*node.m_spMesh, subset, center, extents)) continue;

			const float axisExtents[3] = { extents.x, extents.y, extents.z };
			const Math::Vector3 baseAxes[3] = { Math::Vector3::Right, Math::Vector3::Up, Math::Vector3::Backward };

			// 最も薄い軸を法線とみなすと、平面モデルでも箱形モデルでも一貫して鏡面を取り出しやすい。
			int axisOrder[3] = { 0, 1, 2 };
			for (int i = 0; i < 3; ++i)
			{
				for (int j = i + 1; j < 3; ++j)
				{
					if (axisExtents[axisOrder[i]] < axisExtents[axisOrder[j]])
					{
						std::swap(axisOrder[i], axisOrder[j]);
					}
				}
			}

			const int rightAxis = axisOrder[0];
			const int upAxis = axisOrder[1];
			const int normalAxis = axisOrder[2];

			Math::Vector3 rawRight = Math::Vector3::TransformNormal(baseAxes[rightAxis], node.m_worldTransform);
			Math::Vector3 rawUp = Math::Vector3::TransformNormal(baseAxes[upAxis], node.m_worldTransform);
			Math::Vector3 rawNormal = Math::Vector3::TransformNormal(baseAxes[normalAxis], node.m_worldTransform);

			float rightLength = rawRight.Length();
			float upLength = rawUp.Length();
			if (rightLength <= 0.0f || upLength <= 0.0f || rawNormal.LengthSquared() <= 0.0f) continue;

			rawRight /= rightLength;
			rawUp /= upLength;
			rawNormal.Normalize();

			// 同一モデル内に複数候補がある場合は、より金属度が高く粗さの低い面を優先する。
			const bool isBetterMirror =
				(material.m_metallicRate > bestMetallic) ||
				(material.m_metallicRate == bestMetallic && material.m_roughnessRate < bestRoughness);
			if (!isBetterMirror) continue;

			bestMetallic = material.m_metallicRate;
			bestRoughness = material.m_roughnessRate;
			m_hasMirrorSurface = true;

			m_planeCenterModel = Math::Vector3::Transform(center, node.m_worldTransform);
			m_planeRightModel = rawRight;
			m_planeUpModel = rawUp;
			m_planeNormalModel = rawNormal;
			m_planeHalfWidthModel = axisExtents[rightAxis] * rightLength;
			m_planeHalfHeightModel = axisExtents[upAxis] * upLength;

			// metallic は 0.5 を超えてからゆっくり立ち上げ、1.0 付近だけがはっきり反射する。
			m_reflectionStrength = (material.m_metallicRate - kMirrorMetallicThreshold) / (1.0f - kMirrorMetallicThreshold);
			if (m_reflectionStrength < 0.0f) m_reflectionStrength = 0.0f;
			if (m_reflectionStrength > 1.0f) m_reflectionStrength = 1.0f;
			m_reflectionStrength *= m_reflectionStrength;
			m_reflectionStrength *= kMaxReflectionStrength;

			m_reflectionRoughness = material.m_roughnessRate;
			if (m_reflectionRoughness < 0.0f) m_reflectionRoughness = 0.0f;
			if (m_reflectionRoughness > 1.0f) m_reflectionRoughness = 1.0f;
		}
	}
}

bool Mirror::CalcSubsetBounds(const KdMesh& mesh, const KdMeshSubset& subset, Math::Vector3& center, Math::Vector3& extents) const
{
	const auto& faces = mesh.GetFaces();
	const auto& positions = mesh.GetVertexPositions();

	if (subset.FaceStart >= faces.size() || subset.FaceCount == 0) return false;

	Math::Vector3 minPos = Math::Vector3::Zero;
	Math::Vector3 maxPos = Math::Vector3::Zero;
	bool hasPoint = false;

	const UINT faceEnd = std::min(static_cast<UINT>(faces.size()), subset.FaceStart + subset.FaceCount);
	for (UINT faceIdx = subset.FaceStart; faceIdx < faceEnd; ++faceIdx)
	{
		const auto& face = faces[faceIdx];
		for (int i = 0; i < 3; ++i)
		{
			if (face.Idx[i] >= positions.size()) continue;

			const Math::Vector3& pos = positions[face.Idx[i]];
			if (!hasPoint)
			{
				minPos = pos;
				maxPos = pos;
				hasPoint = true;
				continue;
			}

			minPos.x = std::min(minPos.x, pos.x);
			minPos.y = std::min(minPos.y, pos.y);
			minPos.z = std::min(minPos.z, pos.z);
			maxPos.x = std::max(maxPos.x, pos.x);
			maxPos.y = std::max(maxPos.y, pos.y);
			maxPos.z = std::max(maxPos.z, pos.z);
		}
	}

	if (!hasPoint) return false;

	center = (minPos + maxPos) * 0.5f;
	extents = (maxPos - minPos) * 0.5f;
	return true;
}
