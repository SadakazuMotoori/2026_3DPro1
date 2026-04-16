#include "KdCollider.h"

#include <array>

// このファイルは大きく 3 つの役割に分かれている。
// 1. 無名名前空間: 複数の形状判定で使い回す補助データと計算関数
// 2. KdCollider 本体: 登録済み形状の振り分けと共通インターフェース
// 3. 各 Collision クラス: 形状ごとの実際の当たり判定処理
namespace
{
	// カプセルの押し戻しを一度で終えず、最大何回まで段階的に解決するか。
	constexpr int kCapsuleSolveIteration = 4;

	// カプセルを複数球へ分解する時の最大サンプル数。
	// 精度を上げすぎて極端に重くならないよう上限を設けている。
	constexpr int kMaxCapsuleSampleCount = 16;

	// AABB を OBB と同じ処理で扱う時に使う単位クォータニオン。
	const Math::Vector4 kIdentityQuaternion(0.0f, 0.0f, 0.0f, 1.0f);

	// 実際の判定に使いやすい形へ展開したカプセル情報。
	// center / up / start / end が決まっていると、球・箱・カプセルとの判定を共通化しやすい。
	struct CapsuleShapeData
	{
		Math::Vector3 m_center = Math::Vector3::Zero;
		Math::Vector3 m_up = Math::Vector3::Up;
		Math::Vector3 m_start = Math::Vector3::Zero;
		Math::Vector3 m_end = Math::Vector3::Zero;
		float m_radius = 0.0f;
		float m_height = 0.0f;
		float m_cylinderLength = 0.0f;
		float m_broadRadius = 0.0f;
	};

	// カプセルを縦方向に並ぶ複数球へ分解した時の中心群。
	// メッシュや BOX との詳細判定で使い、カプセル専用の低レベル判定を持たない処理を補う。
	struct CapsuleSamplePoints
	{
		std::array<Math::Vector3, kMaxCapsuleSampleCount> m_centers = {};
		int m_count = 0;
	};

	// 球と BOX の最近接情報をまとめた中間結果。
	// まずこの形で情報を揃えておくことで、AABB / OBB の両方を同じ後処理で扱える。
	struct SphereBoxContact
	{
		Math::Vector3 m_nearestPos = Math::Vector3::Zero;
		Math::Vector3 m_hitDir = Math::Vector3::Zero;
		float m_overlapDistance = 0.0f;
		bool m_isInside = false;
	};

	// モデル判定で、各衝突メッシュに対して毎回再計算したくない情報をまとめたキャッシュ。
	struct ModelCollisionCache
	{
		const KdMesh* m_mesh = nullptr;
		Math::Matrix m_world = Math::Matrix::Identity;
		DirectX::BoundingBox m_aabb = {};
	};

	// 与えられた方向ベクトルが使えない時、順番に代替候補を試して
	// 「結果として必ず正規化済みの向きが 1 本返る」ようにする補助関数。
	Math::Vector3 NormalizeOrFallback(Math::Vector3 dir, const Math::Vector3& fallback1, const Math::Vector3& fallback2, const Math::Vector3& fallback3)
	{
		if (dir.LengthSquared() > KdCollisionEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback1;
		if (dir.LengthSquared() > KdCollisionEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback2;
		if (dir.LengthSquared() > KdCollisionEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback3;
		if (dir.LengthSquared() > KdCollisionEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		return Math::Vector3::Up;
	}

	// 負の値を 0 に丸める。
	// 半径や長さのように、本来マイナスであってはいけない値を安全側へ寄せるために使う。
	float ClampNonNegative(float value)
	{
		return (value < 0.0f) ? 0.0f : value;
	}

	// 点から線分への最近接点を返す。
	// カプセルは「線分 + 半径」とみなせるため、球やカプセルとの距離判定の基礎になる。
	Math::Vector3 ClosestPointOnSegment(const Math::Vector3& point, const Math::Vector3& start, const Math::Vector3& end)
	{
		Math::Vector3 segment = end - start;
		float segmentLengthSqr = segment.LengthSquared();
		if (segmentLengthSqr <= KdCollisionEpsilon)
		{
			return start;
		}

		float t = DirectX::XMVector3Dot(point - start, segment).m128_f32[0] / segmentLengthSqr;
		if (t < 0.0f) { t = 0.0f; }
		else if (t > 1.0f) { t = 1.0f; }

		return start + segment * t;
	}

	// 低レベルのメッシュ判定結果を KdCollider の共通結果へ写す。
	void CopyCollisionMeshResult(const CollisionMeshResult& src, KdCollider::CollisionResult& dst)
	{
		dst.m_hitPos = src.m_hitPos;
		dst.m_hitDir = src.m_hitDir;
		dst.m_hitNDir = src.m_hitNDir;
		dst.m_overlapDistance = src.m_overlapDistance;
	}

	// 既に完成している衝突結果をコピーしつつ、
	// 必要なら向きだけ反転して「呼び出し側の規約」に合わせる。
	void CopyCollisionResult(const KdCollider::CollisionResult& src, KdCollider::CollisionResult& dst, bool invertDirection)
	{
		dst = src;

		if (invertDirection)
		{
			dst.m_hitDir *= -1.0f;
			dst.m_hitNDir *= -1.0f;
		}
	}

	// 「中心同士の直線距離で考えられる形状」のヒット結果を共通の式で埋める。
	// 球 vs 球やカプセル端点ベースの判定で、押し出し方向と重なり量の作り方を統一するために使う。
	void FillLinearHitResult(
		const Math::Vector3& basePos,
		float baseRadius,
		const Math::Vector3& targetPos,
		float needDistance,
		KdCollider::CollisionResult& outResult,
		const Math::Vector3& fallback1,
		const Math::Vector3& fallback2,
		const Math::Vector3& fallback3)
	{
		const Math::Vector3 hitVec = targetPos - basePos;
		const float betweenDistance = hitVec.Length();

		outResult.m_hitDir = NormalizeOrFallback(hitVec, fallback1, fallback2, fallback3);
		outResult.m_overlapDistance = needDistance - betweenDistance;
		outResult.m_hitPos = basePos + outResult.m_hitDir * (baseRadius + outResult.m_overlapDistance * 0.5f);
		outResult.m_hitNDir = outResult.m_hitDir;
	}

	// カプセルの基本情報から、中心軸や両端点など
	// 以降の判定処理が直接使う形へ展開する。
	CapsuleShapeData BuildCapsuleShapeData(
		const Math::Vector3& center,
		Math::Vector3 upAxis,
		float upScale,
		float radiusScale,
		float height,
		float radius)
	{
		CapsuleShapeData result;
		result.m_center = center;

		if (upScale <= KdCollisionEpsilon || upAxis.LengthSquared() <= KdCollisionEpsilon)
		{
			// 軸情報が壊れている時は Y 軸上向きを仮採用し、
			// 少なくとも「高さ方向を持つカプセル」として扱えるようにする。
			result.m_up = Math::Vector3::Up;
			upScale = 1.0f;
		}
		else
		{
			result.m_up = upAxis;
			result.m_up.Normalize();
		}

		if (radiusScale <= KdCollisionEpsilon)
		{
			radiusScale = 1.0f;
		}

		result.m_radius = ClampNonNegative(radius) * radiusScale;
		result.m_height = ClampNonNegative(height) * upScale;
		if (result.m_height < result.m_radius * 2.0f)
		{
			// 高さが直径より小さい時は、潰れたカプセルではなく球として扱える最小サイズへ丸める
			result.m_height = result.m_radius * 2.0f;
		}

		result.m_cylinderLength = result.m_height - result.m_radius * 2.0f;
		result.m_start = result.m_center - result.m_up * (result.m_cylinderLength * 0.5f);
		result.m_end = result.m_center + result.m_up * (result.m_cylinderLength * 0.5f);
		result.m_broadRadius = result.m_radius + result.m_cylinderLength * 0.5f;

		return result;
	}

	// 登録側カプセルに所有者のワールド行列を適用し、
	// 実際にワールド空間へ置かれたカプセル形状を作る。
	CapsuleShapeData BuildCapsuleShapeData(const KdCollider::CapsuleInfo& capsule, const Math::Matrix& world)
	{
		const Math::Vector3 center = Math::Vector3::Transform(capsule.m_pos + capsule.m_offset, world);
		const Math::Vector3 upAxis = world.Up();
		const float upScale = upAxis.Length();
		float radiusScale = world.Right().Length();
		const float backwardScale = world.Backward().Length();
		if (backwardScale > radiusScale)
		{
			// X/Z のうち太い方を半径スケールに使い、
			// 非等方スケール時でもカプセルが細くなりすぎないようにする。
			radiusScale = backwardScale;
		}

		return BuildCapsuleShapeData(center, upAxis, upScale, radiusScale, capsule.m_height, capsule.m_radius);
	}

	// クエリ側カプセルを、そのままワールド座標にあるものとして展開する。
	CapsuleShapeData BuildCapsuleShapeData(const KdCollider::CapsuleInfo& capsule)
	{
		return BuildCapsuleShapeData(
			capsule.m_pos + capsule.m_offset,
			Math::Vector3::Up,
			1.0f,
			1.0f,
			capsule.m_height,
			capsule.m_radius
		);
	}

	// カプセルを複数の球中心へ分解する。
	// 低レベルの「球 vs 何か」判定を再利用したい時の近似表現として使う。
	CapsuleSamplePoints BuildCapsuleSamplePoints(const CapsuleShapeData& capsule)
	{
		CapsuleSamplePoints result;

		if (capsule.m_cylinderLength <= KdCollisionEpsilon)
		{
			// 高さがほぼ直径しかないカプセルは、実質球として 1 サンプルで十分。
			result.m_centers[0] = capsule.m_center;
			result.m_count = 1;
			return result;
		}

		float sampleStep = capsule.m_radius * 0.5f;
		if (sampleStep <= KdCollisionEpsilon)
		{
			sampleStep = capsule.m_cylinderLength;
		}

		// 半径の半分程度の間隔で球を並べると、精度とコストのバランスがよい。
		result.m_count = static_cast<int>(std::ceil(capsule.m_cylinderLength / sampleStep)) + 1;
		result.m_count = std::clamp(result.m_count, 2, kMaxCapsuleSampleCount);

		const Math::Vector3 capsuleAxis = capsule.m_end - capsule.m_start;
		for (int i = 0; i < result.m_count; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(result.m_count - 1);
			result.m_centers[i] = capsule.m_start + capsuleAxis * t;
		}

		return result;
	}

	// カプセル全体をざっくり包む球を作る。
	// 詳細判定前のブロードフェイズで「明らかに遠い相手」を素早く除外するために使う。
	DirectX::BoundingSphere BuildBroadSphere(const CapsuleShapeData& capsule)
	{
		DirectX::BoundingSphere result;
		result.Center = capsule.m_center;
		result.Radius = capsule.m_broadRadius;
		return result;
	}

	// 球と BOX の最近接点・押し出し方向・重なり量を求める。
	// 外側から当たった場合と、球が BOX の内側にいる場合とで処理を分けている。
	bool ComputeSphereBoxContact(
		const Math::Vector3& sphereCenter,
		float sphereRadius,
		const Math::Vector3& boxCenter,
		const Math::Vector3& boxExtents,
		const Math::Vector4& boxOrientation,
		const Math::Vector3& fallback1,
		const Math::Vector3& fallback2,
		const Math::Vector3& fallback3,
		SphereBoxContact& outContact)
	{
		const Math::Vector3 localCenter = XMVector3InverseRotate(sphereCenter - boxCenter, boxOrientation);

		const bool isInside =
			(localCenter.x >= -boxExtents.x && localCenter.x <= boxExtents.x) &&
			(localCenter.y >= -boxExtents.y && localCenter.y <= boxExtents.y) &&
			(localCenter.z >= -boxExtents.z && localCenter.z <= boxExtents.z);
		outContact.m_isInside = isInside;

		if (!isInside)
		{
			// 外側にいる時は、球中心を BOX 内へクランプした点が最近接点になる。
			const Math::Vector3 nearestLocal(
				std::clamp(localCenter.x, -boxExtents.x, boxExtents.x),
				std::clamp(localCenter.y, -boxExtents.y, boxExtents.y),
				std::clamp(localCenter.z, -boxExtents.z, boxExtents.z)
			);

			const Math::Vector3 toBoxLocal = nearestLocal - localCenter;
			const float distSqr = toBoxLocal.LengthSquared();
			if (distSqr > sphereRadius * sphereRadius)
			{
				return false;
			}

			outContact.m_nearestPos = Math::Vector3(XMVector3Rotate(nearestLocal, boxOrientation)) + boxCenter;
			outContact.m_hitDir = NormalizeOrFallback(outContact.m_nearestPos - sphereCenter, fallback1, fallback2, fallback3);
			outContact.m_overlapDistance = sphereRadius - std::sqrt(distSqr);
			return true;
		}

		// 内側にいる時は「最も近い面へどちらに抜けるか」を探し、
		// 最短で外へ出せる向きと距離を返す。
		float nearestFaceDist = localCenter.x + boxExtents.x;
		Math::Vector3 hitDirLocal(-1.0f, 0.0f, 0.0f);
		Math::Vector3 nearestLocal(-boxExtents.x, localCenter.y, localCenter.z);

		const float distToMaxX = boxExtents.x - localCenter.x;
		if (distToMaxX < nearestFaceDist)
		{
			nearestFaceDist = distToMaxX;
			hitDirLocal = Math::Vector3(1.0f, 0.0f, 0.0f);
			nearestLocal = Math::Vector3(boxExtents.x, localCenter.y, localCenter.z);
		}

		const float distToMinY = localCenter.y + boxExtents.y;
		if (distToMinY < nearestFaceDist)
		{
			nearestFaceDist = distToMinY;
			hitDirLocal = Math::Vector3(0.0f, -1.0f, 0.0f);
			nearestLocal = Math::Vector3(localCenter.x, -boxExtents.y, localCenter.z);
		}

		const float distToMaxY = boxExtents.y - localCenter.y;
		if (distToMaxY < nearestFaceDist)
		{
			nearestFaceDist = distToMaxY;
			hitDirLocal = Math::Vector3(0.0f, 1.0f, 0.0f);
			nearestLocal = Math::Vector3(localCenter.x, boxExtents.y, localCenter.z);
		}

		const float distToMinZ = localCenter.z + boxExtents.z;
		if (distToMinZ < nearestFaceDist)
		{
			nearestFaceDist = distToMinZ;
			hitDirLocal = Math::Vector3(0.0f, 0.0f, -1.0f);
			nearestLocal = Math::Vector3(localCenter.x, localCenter.y, -boxExtents.z);
		}

		const float distToMaxZ = boxExtents.z - localCenter.z;
		if (distToMaxZ < nearestFaceDist)
		{
			nearestFaceDist = distToMaxZ;
			hitDirLocal = Math::Vector3(0.0f, 0.0f, 1.0f);
			nearestLocal = Math::Vector3(localCenter.x, localCenter.y, boxExtents.z);
		}

		outContact.m_nearestPos = Math::Vector3(XMVector3Rotate(nearestLocal, boxOrientation)) + boxCenter;
		outContact.m_hitDir = NormalizeOrFallback(
			Math::Vector3(XMVector3Rotate(hitDirLocal, boxOrientation)),
			fallback1,
			fallback2,
			fallback3
		);
		outContact.m_overlapDistance = sphereRadius + nearestFaceDist;

		return true;
	}

	// カプセル vs BOX を、複数球サンプルに分解した近似で解く。
	// 一度に全補正を加えると押し戻しが暴れやすいため、最も強い補正だけを数回反復適用する。
	bool ResolveCapsuleVsBox(
		const CapsuleShapeData& capsule,
		const CapsuleSamplePoints& samples,
		const Math::Vector3& boxCenter,
		const Math::Vector3& boxExtents,
		const Math::Vector4& boxOrientation,
		const Math::Vector3& fallback1,
		const Math::Vector3& fallback2,
		KdCollider::CollisionResult* pRes)
	{
		bool isHit = false;
		Math::Vector3 totalCorrection = Math::Vector3::Zero;
		Math::Vector3 hitPos = Math::Vector3::Zero;
		Math::Vector3 hitNDir = Math::Vector3::Zero;

		for (int solve = 0; solve < kCapsuleSolveIteration; ++solve)
		{
			// 今の押し戻し後の位置で最も深くめり込んでいるサンプル球を探す。
			bool hitThisSolve = false;
			Math::Vector3 bestCorrection = Math::Vector3::Zero;
			Math::Vector3 bestHitPos = Math::Vector3::Zero;
			Math::Vector3 bestHitNDir = Math::Vector3::Zero;

			for (int i = 0; i < samples.m_count; ++i)
			{
				const Math::Vector3 sphereCenter = samples.m_centers[i] + totalCorrection;

				SphereBoxContact contact;
				if (!ComputeSphereBoxContact(
					sphereCenter,
					capsule.m_radius,
					boxCenter,
					boxExtents,
					boxOrientation,
					boxCenter - sphereCenter,
					fallback1,
					fallback2,
					contact))
				{
					continue;
				}

				if (!pRes)
				{
					return true;
				}

				hitThisSolve = true;
				isHit = true;

				// 実際の分離は「BOX -> カプセル」方向へ行うので、返却規約用の向きを反転して使う
				const Math::Vector3 correction = contact.m_hitDir * -contact.m_overlapDistance;
				if (correction.LengthSquared() > bestCorrection.LengthSquared())
				{
					bestCorrection = correction;
					bestHitPos = sphereCenter + contact.m_hitDir * (capsule.m_radius + contact.m_overlapDistance * 0.5f);
					bestHitNDir = contact.m_hitDir;
				}
			}

			if (!hitThisSolve || bestCorrection.LengthSquared() <= KdCollisionEpsilon)
			{
				break;
			}

			totalCorrection += bestCorrection;
			hitPos = bestHitPos;
			hitNDir = bestHitNDir;
		}

		if (pRes && isHit)
		{
			// totalCorrection は「実際にカプセルをどちらへ動かしたか」なので、
			// 返却規約に合わせて hitDir には逆向きを採用する。
			pRes->m_overlapDistance = totalCorrection.Length();
			pRes->m_hitDir = NormalizeOrFallback(-totalCorrection, hitNDir, fallback1, fallback2);
			pRes->m_hitPos = hitPos;
			pRes->m_hitNDir = hitNDir;
		}

		return isHit;
	}

	// OBB のローカル X/Y/Z 軸をワールド空間へ展開する。
	// SAT では箱の各軸がそのまま候補分離軸になる。
	void BuildObbAxes(const DirectX::BoundingOrientedBox& box, Math::Vector3 outAxes[3])
	{
		const Math::Vector4 quat(box.Orientation);
		outAxes[0] = Math::Vector3(XMVector3Rotate(Math::Vector3(1.0f, 0.0f, 0.0f), quat));
		outAxes[1] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 1.0f, 0.0f), quat));
		outAxes[2] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 0.0f, 1.0f), quat));

		outAxes[0].Normalize();
		outAxes[1].Normalize();
		outAxes[2].Normalize();
	}

	// 指定方向へ最も張り出した OBB 上の点を返す。
	// 接触位置をざっくり出したい時の「サポート点」として使う。
	Math::Vector3 GetSupportPoint(const DirectX::BoundingOrientedBox& box, const Math::Vector3 axes[3], const Math::Vector3& dir)
	{
		Math::Vector3 support = box.Center;
		const float extents[3] = { box.Extents.x, box.Extents.y, box.Extents.z };

		for (int i = 0; i < 3; ++i)
		{
			const float sign = (DirectX::XMVector3Dot(dir, axes[i]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;
			support += axes[i] * extents[i] * sign;
		}

		return support;
	}

	// AABB を回転なし OBB として扱える形へ変換する。
	// BOX vs BOX の詳細結果は OBB ベースの共通処理へ寄せているため、その前準備として使う。
	DirectX::BoundingOrientedBox MakeObbFromAabb(const DirectX::BoundingBox& box)
	{
		DirectX::BoundingOrientedBox result;
		DirectX::BoundingOrientedBox::CreateFromBoundingBox(result, box);
		return result;
	}

	// OBB 同士の重なりから、最小押し出し方向と重なり量を SAT で求める。
	// 判定だけでなく「どちらへどれだけ押し出すか」を返すのが主目的。
	bool ComputeBoxPenetrationResult(
		const DirectX::BoundingOrientedBox& myBox,
		const DirectX::BoundingOrientedBox& targetBox,
		const Math::Vector3& fallback1,
		const Math::Vector3& fallback2,
		KdCollider::CollisionResult& outResult)
	{
		Math::Vector3 myAxes[3];
		Math::Vector3 targetAxes[3];
		BuildObbAxes(myBox, myAxes);
		BuildObbAxes(targetBox, targetAxes);

		const float myExtents[3] = { myBox.Extents.x, myBox.Extents.y, myBox.Extents.z };
		const float targetExtents[3] = { targetBox.Extents.x, targetBox.Extents.y, targetBox.Extents.z };
		const Math::Vector3 centerDelta = Math::Vector3(targetBox.Center) - Math::Vector3(myBox.Center);

		float t[3] =
		{
			DirectX::XMVector3Dot(centerDelta, myAxes[0]).m128_f32[0],
			DirectX::XMVector3Dot(centerDelta, myAxes[1]).m128_f32[0],
			DirectX::XMVector3Dot(centerDelta, myAxes[2]).m128_f32[0]
		};

		float rot[3][3] = {};
		float absRot[3][3] = {};
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				rot[i][j] = DirectX::XMVector3Dot(myAxes[i], targetAxes[j]).m128_f32[0];
				absRot[i][j] = fabsf(rot[i][j]) + KdCollisionEpsilon;
			}
		}

		float minOverlap = FLT_MAX;
		Math::Vector3 bestAxis = Math::Vector3::Zero;

		// 候補軸ごとに重なり量を見て、最も小さい軸を最終結果として採用する。
		// 1 本でも分離軸が見つかれば、そもそも衝突していない。
		auto updateBestAxis = [&](Math::Vector3 axis, float overlap)
		{
			if (overlap < 0.0f)
			{
				if (overlap < -KdCollisionEpsilon)
				{
					return false;
				}

				overlap = 0.0f;
			}

			if (axis.LengthSquared() <= KdCollisionEpsilon)
			{
				return true;
			}

			axis.Normalize();
			if (overlap < minOverlap)
			{
				minOverlap = overlap;
				bestAxis = axis;
			}

			return true;
		};

		// 候補 1: 自分の 3 軸
		for (int i = 0; i < 3; ++i)
		{
			const float ra = myExtents[i];
			const float rb =
				targetExtents[0] * absRot[i][0] +
				targetExtents[1] * absRot[i][1] +
				targetExtents[2] * absRot[i][2];

			const float overlap = ra + rb - fabsf(t[i]);
			const float sign = (t[i] >= 0.0f) ? 1.0f : -1.0f;
			if (!updateBestAxis(myAxes[i] * sign, overlap))
			{
				return false;
			}
		}

		// 候補 2: 相手の 3 軸
		for (int j = 0; j < 3; ++j)
		{
			const float ra =
				myExtents[0] * absRot[0][j] +
				myExtents[1] * absRot[1][j] +
				myExtents[2] * absRot[2][j];
			const float rb = targetExtents[j];
			const float projectedCenter =
				t[0] * rot[0][j] +
				t[1] * rot[1][j] +
				t[2] * rot[2][j];
			const float overlap = ra + rb - fabsf(projectedCenter);
			const float sign = (DirectX::XMVector3Dot(centerDelta, targetAxes[j]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;
			if (!updateBestAxis(targetAxes[j] * sign, overlap))
			{
				return false;
			}
		}

		// 候補 3: 各軸の外積で得られる 9 軸
		// エッジ同士の食い込みはこの候補を見ないと拾えない。
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				const Math::Vector3 axis = myAxes[i].Cross(targetAxes[j]);
				if (axis.LengthSquared() <= KdCollisionEpsilon)
				{
					continue;
				}

				const float ra =
					myExtents[(i + 1) % 3] * absRot[(i + 2) % 3][j] +
					myExtents[(i + 2) % 3] * absRot[(i + 1) % 3][j];
				const float rb =
					targetExtents[(j + 1) % 3] * absRot[i][(j + 2) % 3] +
					targetExtents[(j + 2) % 3] * absRot[i][(j + 1) % 3];
				const float dist = fabsf(
					t[(i + 2) % 3] * rot[(i + 1) % 3][j] -
					t[(i + 1) % 3] * rot[(i + 2) % 3][j]
				);
				const float overlap = ra + rb - dist;
				const float sign = (DirectX::XMVector3Dot(centerDelta, axis).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;
				if (!updateBestAxis(axis * sign, overlap))
				{
					return false;
				}
			}
		}

		outResult.m_hitDir = NormalizeOrFallback(bestAxis, centerDelta, fallback1, fallback2);
		outResult.m_overlapDistance = minOverlap;
		outResult.m_hitPos =
			(GetSupportPoint(myBox, myAxes, outResult.m_hitDir) + GetSupportPoint(targetBox, targetAxes, -outResult.m_hitDir)) * 0.5f;
		outResult.m_hitNDir = outResult.m_hitDir;

		return true;
	}

	// 2 本の線分の最近接点同士を求める。
	// カプセル vs カプセルは「中心線分同士の最短距離」が分かれば判定できる。
	void ClosestPointsBetweenSegments(
		const Math::Vector3& start1,
		const Math::Vector3& end1,
		const Math::Vector3& start2,
		const Math::Vector3& end2,
		Math::Vector3& outPoint1,
		Math::Vector3& outPoint2)
	{
		const Math::Vector3 d1 = end1 - start1;
		const Math::Vector3 d2 = end2 - start2;
		const Math::Vector3 r = start1 - start2;

		const float a = DirectX::XMVector3Dot(d1, d1).m128_f32[0];
		const float e = DirectX::XMVector3Dot(d2, d2).m128_f32[0];
		const float f = DirectX::XMVector3Dot(d2, r).m128_f32[0];

		float s = 0.0f;
		float t = 0.0f;

		if (a <= KdCollisionEpsilon && e <= KdCollisionEpsilon)
		{
			s = 0.0f;
			t = 0.0f;
		}
		else if (a <= KdCollisionEpsilon)
		{
			t = std::clamp(f / e, 0.0f, 1.0f);
		}
		else
		{
			const float c = DirectX::XMVector3Dot(d1, r).m128_f32[0];
			if (e <= KdCollisionEpsilon)
			{
				s = std::clamp(-c / a, 0.0f, 1.0f);
			}
			else
			{
				const float b = DirectX::XMVector3Dot(d1, d2).m128_f32[0];
				const float denom = a * e - b * b;

				if (fabsf(denom) > KdCollisionEpsilon)
				{
					s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
				}

				t = (b * s + f) / e;

				if (t < 0.0f)
				{
					t = 0.0f;
					s = std::clamp(-c / a, 0.0f, 1.0f);
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
					s = std::clamp((b - c) / a, 0.0f, 1.0f);
				}
			}
		}

		outPoint1 = start1 + d1 * s;
		outPoint2 = start2 + d2 * t;
	}

	bool IntersectRayCapsule(
		const KdCollider::RayInfo& ray,
		const CapsuleShapeData& capsule,
		float& outHitDistance,
		Math::Vector3& outHitPos,
		Math::Vector3& outHitNDir,
		const Math::Vector3& fallback1,
		const Math::Vector3& fallback2,
		const Math::Vector3& fallback3)
	{
		if (capsule.m_cylinderLength <= KdCollisionEpsilon)
		{
			DirectX::BoundingSphere sphere;
			sphere.Center = capsule.m_center;
			sphere.Radius = capsule.m_radius;

			float hitDistance = 0.0f;
			if (!sphere.Intersects(ray.m_pos, ray.m_dir, hitDistance) || hitDistance > ray.m_range)
			{
				return false;
			}

			outHitDistance = hitDistance;
			outHitPos = ray.m_pos + ray.m_dir * hitDistance;
			outHitNDir = NormalizeOrFallback(outHitPos - capsule.m_center, fallback1, fallback2, fallback3);
			return true;
		}

		const Math::Vector3 startClosest = ClosestPointOnSegment(ray.m_pos, capsule.m_start, capsule.m_end);
		const Math::Vector3 startToCapsule = ray.m_pos - startClosest;
		if (startToCapsule.LengthSquared() <= capsule.m_radius * capsule.m_radius)
		{
			outHitDistance = 0.0f;
			outHitPos = ray.m_pos;
			outHitNDir = NormalizeOrFallback(startToCapsule, fallback1, fallback2, fallback3);
			return true;
		}

		float bestHitDistance = FLT_MAX;
		Math::Vector3 bestHitPos = Math::Vector3::Zero;
		Math::Vector3 bestHitNDir = Math::Vector3::Zero;

		auto updateSphereHit = [&](const Math::Vector3& center)
		{
			DirectX::BoundingSphere sphere;
			sphere.Center = center;
			sphere.Radius = capsule.m_radius;

			float hitDistance = 0.0f;
			if (!sphere.Intersects(ray.m_pos, ray.m_dir, hitDistance))
			{
				return;
			}

			if (hitDistance < 0.0f || hitDistance > ray.m_range || hitDistance >= bestHitDistance)
			{
				return;
			}

			bestHitDistance = hitDistance;
			bestHitPos = ray.m_pos + ray.m_dir * hitDistance;
			bestHitNDir = NormalizeOrFallback(bestHitPos - center, fallback1, fallback2, fallback3);
		};

		updateSphereHit(capsule.m_start);
		updateSphereHit(capsule.m_end);

		const Math::Vector3 segment = capsule.m_end - capsule.m_start;
		const float dd = DirectX::XMVector3Dot(segment, segment).m128_f32[0];
		if (dd > KdCollisionEpsilon)
		{
			const Math::Vector3 m = ray.m_pos - capsule.m_start;
			const float md = DirectX::XMVector3Dot(m, segment).m128_f32[0];
			const float nd = DirectX::XMVector3Dot(ray.m_dir, segment).m128_f32[0];
			const float mn = DirectX::XMVector3Dot(m, ray.m_dir).m128_f32[0];
			const float mm = DirectX::XMVector3Dot(m, m).m128_f32[0];

			const float a = dd - nd * nd;
			const float c = dd * mm - md * md - capsule.m_radius * capsule.m_radius * dd;

			if (fabsf(a) > KdCollisionEpsilon)
			{
				const float b = dd * mn - md * nd;
				const float discriminant = b * b - a * c;
				if (discriminant >= 0.0f)
				{
					const float hitDistance = (-b - std::sqrt(discriminant)) / a;
					if (hitDistance >= 0.0f && hitDistance <= ray.m_range && hitDistance < bestHitDistance)
					{
						const float y = md + hitDistance * nd;
						if (y >= 0.0f && y <= dd)
						{
							bestHitDistance = hitDistance;
							bestHitPos = ray.m_pos + ray.m_dir * hitDistance;

							const Math::Vector3 axisPoint = capsule.m_start + segment * (y / dd);
							bestHitNDir = NormalizeOrFallback(bestHitPos - axisPoint, fallback1, fallback2, fallback3);
						}
					}
				}
			}
		}

		if (bestHitDistance == FLT_MAX)
		{
			return false;
		}

		outHitDistance = bestHitDistance;
		outHitPos = bestHitPos;
		outHitNDir = bestHitNDir;
		return true;
	}

	// 登録済み形状を総当たりし、対象 type に合うものだけへ判定を委譲する共通関数。
	// 結果リストが不要なら最初の HIT で即 return し、余計な計算を避ける。
	template <class HitTest>
	bool IntersectsRegisteredShapes(
		const std::unordered_map<std::string, std::unique_ptr<KdCollisionShape>>& collisionShapes,
		UINT targetType,
		int disableType,
		std::list<KdCollider::CollisionResult>* pResults,
		HitTest&& hitTest)
	{
		if ((targetType & disableType) || collisionShapes.empty())
		{
			return false;
		}

		bool isHit = false;

		for (const auto& collisionShapePair : collisionShapes)
		{
			KdCollisionShape* shape = collisionShapePair.second.get();
			if (!shape) { continue; }

			// type が噛み合わない形状は、この問い合わせの対象外。
			if (!(targetType & shape->GetType()))
			{
				continue;
			}

			KdCollider::CollisionResult tmpRes = {};
			KdCollider::CollisionResult* pTmpRes = pResults ? &tmpRes : nullptr;

			if (!hitTest(*shape, pTmpRes))
			{
				continue;
			}

			if (!pResults)
			{
				return true;
			}

			isHit = true;
			pResults->emplace_back(tmpRes);
		}

		return isHit;
	}
}

// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdCollider
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 当たり判定形状の登録関数群
///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, std::unique_ptr<KdCollisionShape> spShape)
{
	if (!spShape) { return; }

	// string_view::data() は終端保証が無いため、そのままキー化せず明示的に std::string へ詰める
	m_collisionShapes.emplace(std::string(name), std::move(spShape));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const DirectX::BoundingSphere& sphere, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdSphereCollision>(sphere, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const DirectX::BoundingBox& box, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdBoxCollision>(box, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const DirectX::BoundingOrientedBox& box, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdBoxCollision>(box, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const CapsuleInfo& capsule)
{
	RegisterCollisionShape(name, std::make_unique<KdCapsuleCollision>(capsule));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const std::shared_ptr<KdModelData>& model, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdModelCollision>(model, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, KdModelData* model, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdModelCollision>(std::shared_ptr<KdModelData>(model), type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const std::shared_ptr<KdModelWork>& model, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdModelCollision>(model, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, KdModelWork* model, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdModelCollision>(std::shared_ptr<KdModelWork>(model), type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, const std::shared_ptr<KdPolygon> polygon, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdPolygonCollision>(polygon, type));
}

///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::RegisterCollisionShape(std::string_view name, KdPolygon* polygon, UINT type)
{
	RegisterCollisionShape(name, std::make_unique<KdPolygonCollision>(std::shared_ptr<KdPolygon>(polygon), type));
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvs球に登録された任意の形状の当たり判定
// 球に合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const SphereInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	return IntersectsRegisteredShapes(
		m_collisionShapes,
		targetShape.m_type,
		m_disableType,
		pResults,
		[&](KdCollisionShape& shape, KdCollider::CollisionResult* pTmpRes)
		{
			return shape.Intersects(targetShape.m_sphere, ownerMatrix, pTmpRes);
		}
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsBOXに登録された任意の形状の当たり判定
// BOXに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const BoxInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	return IntersectsRegisteredShapes(
		m_collisionShapes,
		targetShape.m_type,
		m_disableType,
		pResults,
		[&](KdCollisionShape& shape, KdCollider::CollisionResult* pTmpRes)
		{
			// 呼び出し側の BOX 種別に応じて、AABB / OBB のどちらの仮想関数へ流すかを切り替える。
			if (targetShape.CheckBoxType(BoxInfo::BoxType::BoxAABB))
			{
				return shape.Intersects(targetShape.m_Abox, ownerMatrix, pTmpRes);
			}

			return shape.Intersects(targetShape.m_Obox, ownerMatrix, pTmpRes);
		}
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsレイに登録された任意の形状の当たり判定
// レイに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const RayInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	// レイの方向ベクトルが存在しない場合は判定不能なのでそのまま返る
	if (!targetShape.m_dir.LengthSquared())
	{
		assert(0 && "KdCollider::Intersects：レイの方向ベクトルが存在していないため、正しく判定できません");

		return false;
	}

	return IntersectsRegisteredShapes(
		m_collisionShapes,
		targetShape.m_type,
		m_disableType,
		pResults,
		[&](KdCollisionShape& shape, KdCollider::CollisionResult* pTmpRes)
		{
			return shape.Intersects(targetShape, ownerMatrix, pTmpRes);
		}
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsカプセルに登録された任意の形状の当たり判定
// カプセルに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const CapsuleInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	return IntersectsRegisteredShapes(
		m_collisionShapes,
		targetShape.m_type,
		m_disableType,
		pResults,
		[&](KdCollisionShape& shape, KdCollider::CollisionResult* pTmpRes)
		{
			return shape.Intersects(targetShape, ownerMatrix, pTmpRes);
		}
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 任意のCollisionShapeを検索して有効/無効を切り替える
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::SetEnable(std::string_view name, bool flag)
{
	auto targetCol = m_collisionShapes.find(std::string(name));

	if (targetCol != m_collisionShapes.end())
	{
		targetCol->second->SetEnable(flag);
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 特定のタイプの有効/無効を切り替える
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::SetEnable(int type, bool flag)
{
	// 有効にしたい
	if (flag)
	{
		m_disableType &= ~type;
	}
	// 無効にしたい
	else
	{
		m_disableType |= type;
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 全てのCollisionShapeの有効/無効を一気に切り替える
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::SetEnableAll(bool flag)
{
	for (auto& col : m_collisionShapes)
	{
		col.second->SetEnable(flag);
	}
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// SphereCollision
// 球形の形状
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 球vs球の当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdSphereCollision::Intersects(const DirectX::BoundingSphere& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 登録時のローカル球を、所有者の現在のワールド位置へ変換する。
	DirectX::BoundingSphere myShape;
	m_shape.Transform(myShape, world);

	// 球同士の当たり判定
	const bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		FillLinearHitResult(
			myShape.Center,
			myShape.Radius,
			target.Center,
			myShape.Radius + target.Radius,
			*pRes,
			Math::Vector3(target.Center) - Math::Vector3(myShape.Center),
			world.Right(),
			world.Up()
		);
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 球vsBOX(AABB)の当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdSphereCollision::Intersects(const DirectX::BoundingBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 登録側球をワールド空間へ出してから対象 BOX と判定する。
	DirectX::BoundingSphere myShape;
	m_shape.Transform(myShape, world);

	// 球vsBOXの当たり判定
	const bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// 詳細結果は「球中心に最も近い BOX 上の点」を基準に作る。
		SphereBoxContact contact;
		ComputeSphereBoxContact(
			myShape.Center,
			myShape.Radius,
			target.Center,
			target.Extents,
			kIdentityQuaternion,
			Math::Vector3(target.Center) - Math::Vector3(myShape.Center),
			world.Right(),
			world.Up(),
			contact
		);

		pRes->m_hitDir = contact.m_hitDir;
		pRes->m_overlapDistance = contact.m_overlapDistance;
		pRes->m_hitPos = myShape.Center + pRes->m_hitDir * (myShape.Radius + pRes->m_overlapDistance * 0.5f);
		pRes->m_hitNDir = pRes->m_hitDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 球vsBOX(OBB)の当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdSphereCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 登録側球をワールド空間へ出してから対象 OBB と判定する。
	DirectX::BoundingSphere myShape;
	m_shape.Transform(myShape, world);

	// 球vsBOXの当たり判定
	const bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// OBB でも詳細計算自体は共通関数へ寄せ、向きだけクォータニオンで扱う。
		SphereBoxContact contact;
		ComputeSphereBoxContact(
			myShape.Center,
			myShape.Radius,
			target.Center,
			target.Extents,
			Math::Vector4(target.Orientation),
			Math::Vector3(target.Center) - Math::Vector3(myShape.Center),
			world.Right(),
			world.Up(),
			contact
		);

		pRes->m_hitDir = contact.m_hitDir;
		pRes->m_overlapDistance = contact.m_overlapDistance;
		pRes->m_hitPos = myShape.Center + pRes->m_hitDir * (myShape.Radius + pRes->m_overlapDistance * 0.5f);
		pRes->m_hitNDir = pRes->m_hitDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 球vsレイの当たり判定
// 判定回数は 1 回　計算回数が固定なので処理効率は安定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdSphereCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 登録側球をワールド空間へ出してから、レイとの交差距離を求める。
	DirectX::BoundingSphere myShape;
	m_shape.Transform(myShape, world);

	float hitDistance = 0.0f;

	bool isHit = myShape.Intersects(target.m_pos, target.m_dir, hitDistance);

	// 判定限界距離を加味
	isHit &= (target.m_range >= hitDistance);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// レイ発射位置 + レイの当たった位置までのベクトル 
		pRes->m_hitPos = target.m_pos + target.m_dir * hitDistance;

		pRes->m_hitDir = target.m_dir * (-1);
		pRes->m_hitNDir = pRes->m_hitDir;

		pRes->m_overlapDistance = target.m_range - hitDistance;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 球vsカプセルの当たり判定
// 判定回数は 1 回　計算回数が固定なので処理効率は安定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdSphereCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 登録側球をワールド空間へ出し、その後はカプセル側実装へ処理を寄せる。
	DirectX::BoundingSphere myShape;
	m_shape.Transform(myShape, world);

	// カプセル側の実装を使い、球vsカプセル向けに結果ベクトルだけ反転する
	KdCapsuleCollision capsuleShape(target);

	if (!pRes)
	{
		return capsuleShape.Intersects(myShape, Math::Matrix::Identity, nullptr);
	}

	KdCollider::CollisionResult tmpRes;
	bool isHit = capsuleShape.Intersects(myShape, Math::Matrix::Identity, &tmpRes);
	if (!isHit) { return false; }

	CopyCollisionResult(tmpRes, *pRes, true);

	return true;
}

// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// BOXCollision
// BOXの形状
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvs球の当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const DirectX::BoundingSphere& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	SphereBoxContact contact;
	Math::Vector3 myShapeCenter = Math::Vector3::Zero;
	bool isHit = false;

	if (!m_IsOriented)
	{
		// AABB 登録時は AABB のまま判定し、必要な時だけ詳細結果を作る。
		DirectX::BoundingBox myShape;
		m_Abox.Transform(myShape, world);
		isHit = myShape.Intersects(target);

		if (!pRes || !isHit) { return isHit; }

		myShapeCenter = myShape.Center;
		ComputeSphereBoxContact(
			target.Center,
			target.Radius,
			myShape.Center,
			myShape.Extents,
			kIdentityQuaternion,
			Math::Vector3(myShape.Center) - Math::Vector3(target.Center),
			world.Right(),
			world.Up(),
			contact
		);
	}
	else
	{
		// OBB 登録時は姿勢込みでワールド空間へ展開する。
		DirectX::BoundingOrientedBox myShape;
		m_Obox.Transform(myShape, world);
		isHit = myShape.Intersects(target);

		if (!pRes || !isHit) { return isHit; }

		myShapeCenter = myShape.Center;
		ComputeSphereBoxContact(
			target.Center,
			target.Radius,
			myShape.Center,
			myShape.Extents,
			Math::Vector4(myShape.Orientation),
			Math::Vector3(myShape.Center) - Math::Vector3(target.Center),
			world.Right(),
			world.Up(),
			contact
		);
	}

	// 球がBOXの外側にいる時は従来通り BOX -> 球 方向、
	// 内側にいる時は最短面へ押し出せる向きを優先して安定させる。
	pRes->m_hitDir = contact.m_isInside ? contact.m_hitDir : (contact.m_hitDir * -1.0f);
	pRes->m_overlapDistance = contact.m_overlapDistance;
	pRes->m_hitPos = myShapeCenter + pRes->m_hitDir * (target.Radius + pRes->m_overlapDistance * 0.5f);
	pRes->m_hitNDir = pRes->m_hitDir;

	return true;
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsBOX(AABB)の当たり判定
// AABBを対象にした判定だが、登録側BOXは AABB / OBB のどちらも来るため
// 詳細リザルトが必要な時は両者を OBB として揃え、SAT で押し出し方向を求める
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const DirectX::BoundingBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 最終的な詳細結果は OBB ベースの SAT に寄せるため、まず登録形状を OBB へ揃える。
	DirectX::BoundingOrientedBox myBox;
	bool isHit = false;

	if (!m_IsOriented)
	{
		DirectX::BoundingBox myShape;
		m_Abox.Transform(myShape, world);
		isHit = myShape.Intersects(target);
		if (!pRes || !isHit) { return isHit; }

		myBox = MakeObbFromAabb(myShape);
	}
	else
	{
		m_Obox.Transform(myBox, world);
		isHit = myBox.Intersects(target);
		if (!pRes || !isHit) { return isHit; }
	}

	return ComputeBoxPenetrationResult(myBox, MakeObbFromAabb(target), world.Right(), world.Up(), *pRes);
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsBOX(OBB)の当たり判定
// 対象側が回転付きBOXなので、詳細リザルトが必要な時は
// 登録側BOXも OBB に揃えて SAT で最小分離軸を求める
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// 対象が既に OBB なので、登録側だけ必要に応じて OBB へ揃える。
	DirectX::BoundingOrientedBox myBox;
	bool isHit = false;

	if (!m_IsOriented)
	{
		DirectX::BoundingBox myShape;
		m_Abox.Transform(myShape, world);
		isHit = myShape.Intersects(target);
		if (!pRes || !isHit) { return isHit; }

		myBox = MakeObbFromAabb(myShape);
	}
	else
	{
		m_Obox.Transform(myBox, world);
		isHit = myBox.Intersects(target);
		if (!pRes || !isHit) { return isHit; }
	}

	return ComputeBoxPenetrationResult(myBox, target, world.Right(), world.Up(), *pRes);
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsレイの当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* /*pRes*/)
{
	if (!m_enable) { return false; }

	// 現状は「当たったかどうか」だけを返す実装で、
	// 詳細結果は使っていないため引数名も省略されている。
	float hitDistance = FLT_MAX;
	bool isHit = false;

	if (!m_IsOriented)
	{
		DirectX::BoundingBox myShape;
		m_Abox.Transform(myShape, world);
		isHit = myShape.Intersects(target.m_pos, target.m_dir, hitDistance);
	}
	else
	{
		DirectX::BoundingOrientedBox myShape;
		m_Obox.Transform(myShape, world);
		isHit = myShape.Intersects(target.m_pos, target.m_dir, hitDistance);
	}

	// 他形状と同様に、レイの有効距離外は HIT 扱いしない
	isHit &= (target.m_range >= hitDistance);

	// 即結果を返す(HITしたかどうかだけが知れる)
	return isHit;
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsカプセルの当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// クエリ側カプセルはワールド空間にある前提で、そのまま補助表現へ変換する。
	const CapsuleShapeData capsule = BuildCapsuleShapeData(target);
	const CapsuleSamplePoints samples = BuildCapsuleSamplePoints(capsule);

	KdCollider::CollisionResult tmpRes = {};
	KdCollider::CollisionResult* pTmpRes = pRes ? &tmpRes : nullptr;
	bool isHit = false;

	if (!m_IsOriented)
	{
		DirectX::BoundingBox myShape;
		m_Abox.Transform(myShape, world);

		// カプセル全体を包む球で一度落としてから詳細判定に進む。
		if (!myShape.Intersects(BuildBroadSphere(capsule)))
		{
			return false;
		}

		isHit = ResolveCapsuleVsBox(
			capsule,
			samples,
			myShape.Center,
			myShape.Extents,
			kIdentityQuaternion,
			world.Right(),
			Math::Vector3::Up,
			pTmpRes
		);
	}
	else
	{
		DirectX::BoundingOrientedBox myShape;
		m_Obox.Transform(myShape, world);

		// OBB 側も同様にブロードフェイズで明らかな不一致を先に除外する。
		if (!myShape.Intersects(BuildBroadSphere(capsule)))
		{
			return false;
		}

		isHit = ResolveCapsuleVsBox(
			capsule,
			samples,
			myShape.Center,
			myShape.Extents,
			Math::Vector4(myShape.Orientation),
			world.Right(),
			Math::Vector3::Up,
			pTmpRes
		);
	}

	if (!pRes || !isHit) { return isHit; }

	// ResolveCapsuleVsBox は「カプセル -> BOX」で返すので、BOX側から見た結果に反転する
	CopyCollisionResult(tmpRes, *pRes, true);
	return true;
}

// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ModelCollision
// 3Dメッシュの形状
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvs球の当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const DirectX::BoundingSphere& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	std::shared_ptr<KdModelData> spModelData = m_shape->GetData();

	// データが無ければ判定不能なので返る
	if (!spModelData) { return false; }

	const std::vector<KdModelData::Node>& dataNodes = spModelData->GetOriginalNodes();
	const std::vector<KdModelWork::Node>& workNodes = m_shape->GetNodes();
	const std::vector<int>& collisionNodeIndices = spModelData->GetCollisionMeshNodeIndices();

	// 各メッシュに押される用の球・押される毎に座標を更新する必要がある
	DirectX::BoundingSphere pushedSphere = target;
	// 計算用にFloat3 → Vectorへ変換
	Math::Vector3 pushedSphereCenter = DirectX::XMLoadFloat3(&pushedSphere.Center);

	bool isHit = false;

	Math::Vector3 hitPos;
	Math::Vector3 hitNDir;

	// 当たり判定ノードとのみ当たり判定
	for (int index : collisionNodeIndices)
	{
		const KdModelData::Node& dataNode = dataNodes[index];
		const KdModelWork::Node& workNode = workNodes[index];

		// あり得ないはずだが一応チェック
		if (!dataNode.m_spMesh) { continue; }

		CollisionMeshResult tmpResult;
		CollisionMeshResult* pTmpResult = pRes ? &tmpResult : nullptr;

		// メッシュと球形の当たり判定実行
		if (!MeshIntersect(*dataNode.m_spMesh, pushedSphere, workNode.m_worldTransform * world, pTmpResult))
		{
			continue;
		}

		// 詳細リザルトが必要無ければ即結果を返す
		if (!pRes) { return true; }

		isHit = true;

		// 重なった分押し戻す
		pushedSphereCenter = DirectX::XMVectorAdd(pushedSphereCenter, DirectX::XMVectorScale(tmpResult.m_hitDir, tmpResult.m_overlapDistance));

		DirectX::XMStoreFloat3(&pushedSphere.Center, pushedSphereCenter);

		// とりあえず当たった座標で更新
		hitPos = tmpResult.m_hitPos;

		// 最後に当たった面の法線情報を記憶しておく
		hitNDir = tmpResult.m_hitDir;
	}

	if (pRes && isHit)
	{
		// 最後に当たった座標が使用される
		pRes->m_hitPos = hitPos;

		// 複数のメッシュに押された最終的な位置 - 移動前の位置 = 押し出しベクトル
		pRes->m_hitDir = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&pushedSphere.Center), DirectX::XMLoadFloat3(&target.Center));

		pRes->m_overlapDistance = DirectX::XMVector3Length(pRes->m_hitDir).m128_f32[0];

		pRes->m_hitDir = DirectX::XMVector3Normalize(pRes->m_hitDir);

		// 最後に当たった面の法線が使用される
		pRes->m_hitNDir = hitNDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvsBOX(AABB)の当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const DirectX::BoundingBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	DirectX::BoundingOrientedBox targetBox;
	DirectX::BoundingOrientedBox::CreateFromBoundingBox(targetBox, target);
	return Intersects(targetBox, world, pRes);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvsBOX(OBB)の当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	std::shared_ptr<KdModelData> spModelData = m_shape->GetData();

	// データが無ければ判定不能なので返る
	if (!spModelData) { return false; }

	const std::vector<KdModelData::Node>& dataNodes = spModelData->GetOriginalNodes();
	const std::vector<KdModelWork::Node>& workNodes = m_shape->GetNodes();
	const std::vector<int>& collisionNodeIndices = spModelData->GetCollisionMeshNodeIndices();

	DirectX::BoundingOrientedBox pushedBox = target;
	Math::Vector3 pushedBoxCenter = DirectX::XMLoadFloat3(&pushedBox.Center);

	bool isHit = false;

	Math::Vector3 hitPos;
	Math::Vector3 hitNDir;

	for (int index : collisionNodeIndices)
	{
		const KdModelData::Node& dataNode = dataNodes[index];
		const KdModelWork::Node& workNode = workNodes[index];

		if (!dataNode.m_spMesh) { continue; }

		CollisionMeshResult tmpResult;
		CollisionMeshResult* pTmpResult = pRes ? &tmpResult : nullptr;

		if (!MeshIntersect(*dataNode.m_spMesh, pushedBox, workNode.m_worldTransform * world, pTmpResult))
		{
			continue;
		}

		// 詳細リザルトが必要無ければ即結果を返す
		if (!pRes) { return true; }

		isHit = true;

		pushedBoxCenter = DirectX::XMVectorAdd(pushedBoxCenter, DirectX::XMVectorScale(tmpResult.m_hitDir, tmpResult.m_overlapDistance));
		DirectX::XMStoreFloat3(&pushedBox.Center, pushedBoxCenter);

		hitPos = tmpResult.m_hitPos;
		hitNDir = tmpResult.m_hitNDir;
	}

	if (pRes && isHit)
	{
		pRes->m_hitPos = hitPos;
		pRes->m_hitDir = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&pushedBox.Center), DirectX::XMLoadFloat3(&target.Center));

		pRes->m_overlapDistance = DirectX::XMVector3Length(pRes->m_hitDir).m128_f32[0];
		if (pRes->m_overlapDistance > KdCollisionEpsilon)
		{
			pRes->m_hitDir = DirectX::XMVector3Normalize(pRes->m_hitDir);
		}
		else
		{
			pRes->m_hitDir = hitNDir;
		}

		pRes->m_hitNDir = hitNDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvsレイの当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	std::shared_ptr<KdModelData> spModelData = m_shape->GetData();

	// データが無ければ判定不能なので返る
	if (!spModelData) { return false; }

	CollisionMeshResult nearestResult = {};

	bool isHit = false;

	const std::vector<KdModelData::Node>& dataNodes = spModelData->GetOriginalNodes();
	const std::vector<KdModelWork::Node>& workNodes = m_shape->GetNodes();
	const std::vector<int>& collisionNodeIndices = spModelData->GetCollisionMeshNodeIndices();

	for (int index : collisionNodeIndices)
	{
		const KdModelData::Node& dataNode = dataNodes[index];
		const KdModelWork::Node& workNode = workNodes[index];

		if (!dataNode.m_spMesh) { continue; }

		CollisionMeshResult tmpResult;
		CollisionMeshResult* pTmpResult = pRes ? &tmpResult : nullptr;

		if (!MeshIntersect(*dataNode.m_spMesh, target.m_pos, target.m_dir, target.m_range,
			workNode.m_worldTransform * world, pTmpResult))
		{
			continue;
		}

		// 詳細リザルトが必要無ければ即結果を返す
		if (!pRes) { return true; }

		if (!isHit || tmpResult.m_overlapDistance > nearestResult.m_overlapDistance)
		{
			nearestResult = tmpResult;
		}

		isHit = true;
	}

	if (pRes && isHit)
	{
		// 最も近くで当たったヒット情報をコピーする
		CopyCollisionMeshResult(nearestResult, *pRes);
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvsカプセルの当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	std::shared_ptr<KdModelData> spModelData = m_shape->GetData();

	// データが無ければ判定不能なので返る
	if (!spModelData) { return false; }

	// モデル側にカプセル専用の低レベル判定が無いので、
	// ここではカプセルを縦方向に並んだ複数の球へ分解して既存の球vsメッシュ判定を再利用する。
	const CapsuleShapeData capsule = BuildCapsuleShapeData(target);
	const CapsuleSamplePoints samples = BuildCapsuleSamplePoints(capsule);
	const DirectX::BoundingSphere broadSphere = BuildBroadSphere(capsule);

	bool isHit = false;

	// カプセル全体に最終的にどれだけ押し戻しが入ったかを保持する
	// CollisionResult として返す向きは「登録側カプセル -> 相手BOX」だが、
	// 反復解決の途中では「カプセルをどちらへ動かせば分離できるか」を使わないといけない。
	// この2つはちょうど逆向きなので、内部用には実際の補正ベクトルを別で持つ。
	Math::Vector3 totalPush = Math::Vector3::Zero;
	Math::Vector3 hitPos = Math::Vector3::Zero;
	Math::Vector3 hitNDir = Math::Vector3::Zero;

	const std::vector<KdModelData::Node>& dataNodes = spModelData->GetOriginalNodes();
	const std::vector<KdModelWork::Node>& workNodes = m_shape->GetNodes();
	const std::vector<int>& collisionNodeIndices = spModelData->GetCollisionMeshNodeIndices();

	std::vector<ModelCollisionCache> collisionMeshes;
	collisionMeshes.reserve(collisionNodeIndices.size());
	for (int index : collisionNodeIndices)
	{
		const KdModelData::Node& dataNode = dataNodes[index];
		const KdModelWork::Node& workNode = workNodes[index];
		if (!dataNode.m_spMesh) { continue; }

		ModelCollisionCache cache;
		cache.m_mesh = dataNode.m_spMesh.get();
		cache.m_world = workNode.m_worldTransform * world;
		dataNode.m_spMesh->GetBoundingBox().Transform(cache.m_aabb, cache.m_world);
		collisionMeshes.push_back(cache);
	}

	// 1回で全押し戻しを加算すると、
	// 同じ壁に触れている複数サンプル球が補正を重複させてしまいガタつきやすい。
	// そこで「今の状態で最も強い押し戻し」を1回だけ採用し、必要なら数回繰り返す。
	for (int solve = 0; solve < kCapsuleSolveIteration; ++solve)
	{
		bool hitThisSolve = false;
		Math::Vector3 bestPush = Math::Vector3::Zero;
		Math::Vector3 bestHitPos = Math::Vector3::Zero;
		Math::Vector3 bestHitNDir = Math::Vector3::Zero;

		for (const ModelCollisionCache& collisionMesh : collisionMeshes)
		{
			// メッシュ単位のブロードフェイズ
			DirectX::BoundingSphere currentBroadSphere = broadSphere;
			currentBroadSphere.Center = Math::Vector3(currentBroadSphere.Center) + totalPush;
			if (!collisionMesh.m_aabb.Intersects(currentBroadSphere))
			{
				continue;
			}

			// カプセルを構成する各球で詳細判定する
			for (int i = 0; i < samples.m_count; ++i)
			{
				DirectX::BoundingSphere sampleSphere;
				sampleSphere.Center = samples.m_centers[i] + totalPush;
				sampleSphere.Radius = capsule.m_radius;

				CollisionMeshResult tmpResult;
				CollisionMeshResult* pTmpResult = pRes ? &tmpResult : nullptr;

				if (!MeshIntersect(*collisionMesh.m_mesh, sampleSphere, collisionMesh.m_world, pTmpResult))
				{
					continue;
				}

				// 詳細リザルトが必要無ければ即結果を返す
				if (!pRes) { return true; }

				hitThisSolve = true;
				isHit = true;

				// 今回のサンプル球に対する押し戻しベクトル
				Math::Vector3 push = Math::Vector3(tmpResult.m_hitDir) * tmpResult.m_overlapDistance;

				// 一番強く押し戻す結果だけを採用する
				if (push.LengthSquared() > bestPush.LengthSquared())
				{
					bestPush = push;
					bestHitPos = tmpResult.m_hitPos;
					bestHitNDir = tmpResult.m_hitNDir;
				}
			}
		}

		// これ以上押し出しが必要なければ解決終了
		if (!hitThisSolve || bestPush.LengthSquared() <= KdCollisionEpsilon)
		{
			break;
		}

		totalPush += bestPush;
		hitPos = bestHitPos;
		hitNDir = bestHitNDir;
	}

	if (pRes && isHit)
	{
		pRes->m_hitPos = hitPos;

		// モデルvs球と同じく、最終的にどれだけ押し出されたかを結果として返す
		pRes->m_hitDir = totalPush;
		pRes->m_overlapDistance = pRes->m_hitDir.Length();
		pRes->m_hitDir = NormalizeOrFallback(pRes->m_hitDir, hitNDir, world.Right(), Math::Vector3::Up);

		// 最後に当たった面の法線が使用される
		pRes->m_hitNDir = hitNDir;
	}

	return isHit;
}

// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// PolygonCollision
// 多角形ポリゴン(頂点の集合体)の形状
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vs球の当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const DirectX::BoundingSphere& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	CollisionMeshResult result;
	CollisionMeshResult* pTmpResult = pRes ? &result : nullptr;

	// 実際のポリゴン判定は低レベル関数へ委譲し、
	// ここでは有効判定と結果コピーだけを担当する。
	// メッシュと球形の当たり判定実行
	if (!PolygonsIntersect(*m_shape, target, world, pTmpResult))
	{
		// 当たっていなければ無条件に返る
		return false;
	}

	if (pRes)
	{
		CopyCollisionMeshResult(result, *pRes);
	}

	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vsBOX(AABB)の当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const DirectX::BoundingBox& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	// TODO: 当たり計算は各自必要に応じて拡張して下さい
	return false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vsBOX(OBB)の当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const DirectX::BoundingOrientedBox& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	// TODO: 当たり計算は各自必要に応じて拡張して下さい
	return false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vsレイの当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	CollisionMeshResult result;
	CollisionMeshResult* pTmpResult = pRes ? &result : nullptr;

	// レイ版も球版と同じく、ポリゴン側の共通判定関数を薄く包んでいる。
	if (!PolygonsIntersect(*m_shape, target.m_pos, target.m_dir, target.m_range, world, pTmpResult))
	{
		// 当たっていなければ無条件に返る
		return false;
	}

	if (pRes)
	{
		CopyCollisionMeshResult(result, *pRes);
	}

	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vsカプセルの当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const KdCollider::CapsuleInfo& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	// TODO: 当たり計算は各自必要に応じて拡張して下さい
	return false;
}

// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// CapsuleCollision
// カプセルの形状
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvs球の当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const DirectX::BoundingSphere& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	// カプセルは「線分 + 半径」なので、まず球中心に最も近い線分上の点を求める。
	const CapsuleShapeData capsule = BuildCapsuleShapeData(*m_shape, world);
	const Math::Vector3 closestPos = ClosestPointOnSegment(Math::Vector3(target.Center), capsule.m_start, capsule.m_end);
	const Math::Vector3 capsuleToTarget = Math::Vector3(target.Center) - closestPos;
	const float needDistance = capsule.m_radius + target.Radius;
	const bool isHit = capsuleToTarget.LengthSquared() <= needDistance * needDistance;

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		FillLinearHitResult(
			closestPos,
			capsule.m_radius,
			target.Center,
			needDistance,
			*pRes,
			Math::Vector3(target.Center) - capsule.m_center,
			world.Right(),
			capsule.m_up
		);
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsBOX(AABB)の当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const DirectX::BoundingBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	const CapsuleShapeData capsule = BuildCapsuleShapeData(*m_shape, world);
	// カプセル全体を包む球で先に落とし、重いサンプル判定を減らす。
	if (!target.Intersects(BuildBroadSphere(capsule)))
	{
		return false;
	}

	return ResolveCapsuleVsBox(
		capsule,
		BuildCapsuleSamplePoints(capsule),
		target.Center,
		target.Extents,
		kIdentityQuaternion,
		world.Right(),
		capsule.m_up,
		pRes
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsBOX(OBB)の当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	const CapsuleShapeData capsule = BuildCapsuleShapeData(*m_shape, world);
	// OBB 相手でもブロードフェイズは同じ考え方で行える。
	if (!target.Intersects(BuildBroadSphere(capsule)))
	{
		return false;
	}

	return ResolveCapsuleVsBox(
		capsule,
		BuildCapsuleSamplePoints(capsule),
		target.Center,
		target.Extents,
		Math::Vector4(target.Orientation),
		world.Right(),
		capsule.m_up,
		pRes
	);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsレイの当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable || !m_shape) { return false; }

	const CapsuleShapeData capsule = BuildCapsuleShapeData(*m_shape, world);

	float hitDistance = 0.0f;
	Math::Vector3 hitPos = Math::Vector3::Zero;
	Math::Vector3 hitNDir = Math::Vector3::Zero;
	const bool isHit = IntersectRayCapsule(
		target,
		capsule,
		hitDistance,
		hitPos,
		hitNDir,
		world.Right(),
		capsule.m_up,
		-target.m_dir
	);

	if (!pRes) { return isHit; }

	if (isHit)
	{
		pRes->m_hitPos = hitPos;
		pRes->m_hitDir = target.m_dir * -1.0f;
		pRes->m_hitNDir = hitNDir;
		pRes->m_overlapDistance = target.m_range - hitDistance;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsカプセルの当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable || !m_shape) { return false; }

	// まず両カプセルを「中心線分 + 半径」の形へ展開する。
	const CapsuleShapeData myCapsule = BuildCapsuleShapeData(*m_shape, world);
	const CapsuleShapeData targetCapsule = BuildCapsuleShapeData(target);

	Math::Vector3 myClosest = Math::Vector3::Zero;
	Math::Vector3 targetClosest = Math::Vector3::Zero;
	ClosestPointsBetweenSegments(
		myCapsule.m_start,
		myCapsule.m_end,
		targetCapsule.m_start,
		targetCapsule.m_end,
		myClosest,
		targetClosest
	);

	// 中心線分同士の最近接点間距離が、半径の合計以下なら衝突している。
	const Math::Vector3 hitVec = targetClosest - myClosest;
	const float needDistance = myCapsule.m_radius + targetCapsule.m_radius;
	const bool isHit = hitVec.LengthSquared() <= needDistance * needDistance;

	if (!pRes) { return isHit; }

	if (isHit)
	{
		FillLinearHitResult(
			myClosest,
			myCapsule.m_radius,
			targetClosest,
			needDistance,
			*pRes,
			targetCapsule.m_center - myCapsule.m_center,
			world.Right(),
			myCapsule.m_up
		);
	}

	return isHit;
}
