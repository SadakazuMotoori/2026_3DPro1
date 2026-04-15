#include "KdCollider.h"

namespace
{
	constexpr float kCapsuleEpsilon = 0.0001f;

	Math::Vector3 NormalizeOrFallback(Math::Vector3 dir, const Math::Vector3& fallback1, const Math::Vector3& fallback2, const Math::Vector3& fallback3)
	{
		if (dir.LengthSquared() > kCapsuleEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback1;
		if (dir.LengthSquared() > kCapsuleEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback2;
		if (dir.LengthSquared() > kCapsuleEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		dir = fallback3;
		if (dir.LengthSquared() > kCapsuleEpsilon)
		{
			dir.Normalize();
			return dir;
		}

		return Math::Vector3::Up;
	}

	Math::Vector3 ClosestPointOnSegment(const Math::Vector3& point, const Math::Vector3& start, const Math::Vector3& end)
	{
		Math::Vector3 segment = end - start;
		float segmentLengthSqr = segment.LengthSquared();
		if (segmentLengthSqr <= kCapsuleEpsilon)
		{
			return start;
		}

		float t = DirectX::XMVector3Dot(point - start, segment).m128_f32[0] / segmentLengthSqr;
		if (t < 0.0f) { t = 0.0f; }
		else if (t > 1.0f) { t = 1.0f; }

		return start + segment * t;
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

	m_collisionShapes.emplace(name.data(), std::move(spShape));
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
	// 当たり判定無効のタイプの場合は返る
	if (targetShape.m_type & m_disableType) { return false; }

	bool isHit = false;

	for (auto& collisionShape : m_collisionShapes)
	{
		// 用途が一致していない当たり判定形状はスキップ
		if (!(targetShape.m_type & collisionShape.second->GetType())) { continue; }

		KdCollider::CollisionResult tmpRes;
		KdCollider::CollisionResult* pTmpRes = pResults ? &tmpRes : nullptr;

		if (collisionShape.second->Intersects(targetShape.m_sphere, ownerMatrix, pTmpRes))
		{
			isHit = true;

			// 詳細な衝突結果を必要としない場合は1つでも接触して返す
			if (!pResults) { break; }

			pResults->push_back(tmpRes);
		}
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsBOXに登録された任意の形状の当たり判定
// BOXに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const BoxInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	// 当たり判定無効のタイプの場合は返る
	if (targetShape.m_type & m_disableType) { return false; }

	bool isHit = false;

	for (auto& collisionShape : m_collisionShapes)
	{
		// 用途が一致していない当たり判定形状はスキップ
		if (!(targetShape.m_type & collisionShape.second->GetType())) { continue; }

		KdCollider::CollisionResult tmpRes;
		KdCollider::CollisionResult* pTmpRes = pResults ? &tmpRes : nullptr;

		bool isIntersects = (targetShape.CheckBoxType(BoxInfo::BoxType::BoxAABB)) ? collisionShape.second->Intersects(targetShape.m_Abox, ownerMatrix, pTmpRes) :
			collisionShape.second->Intersects(targetShape.m_Obox, ownerMatrix, pTmpRes);
		if (isIntersects)
		{
			isHit = true;

			// 詳細な衝突結果を必要としない場合は1つでも接触して返す
			if (!pResults) { break; }

			pResults->push_back(tmpRes);
		}
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsレイに登録された任意の形状の当たり判定
// レイに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const RayInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	// 当たり判定無効のタイプの場合は返る
	if (targetShape.m_type & m_disableType) { return false; }

	// レイの方向ベクトルが存在しない場合は判定不能なのでそのまま返る
	if (!targetShape.m_dir.LengthSquared())
	{
		assert(0 && "KdCollider::Intersects：レイの方向ベクトルが存在していないため、正しく判定できません");

		return false;
	}

	bool isHit = false;

	for (auto& collisionShape : m_collisionShapes)
	{
		// 用途が一致していない当たり判定形状はスキップ
		if (!(targetShape.m_type & collisionShape.second->GetType())) { continue; }

		KdCollider::CollisionResult tmpRes;
		KdCollider::CollisionResult* pTmpRes = pResults ? &tmpRes : nullptr;

		if (collisionShape.second->Intersects(targetShape, ownerMatrix, pTmpRes))
		{
			isHit = true;

			// 詳細な衝突結果を必要としない場合は1つでも接触して返す
			if (!pResults) { break; }

			pResults->push_back(tmpRes);
		}
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// コライダーvsカプセルに登録された任意の形状の当たり判定
// カプセルに合わせて何のために当たり判定をするのか type を渡す必要がある
// 第3引数に詳細結果の受け取る機能が付いている
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCollider::Intersects(const CapsuleInfo& targetShape, const Math::Matrix& ownerMatrix, std::list<KdCollider::CollisionResult>* pResults) const
{
	// 当たり判定無効のタイプの場合は返る
	if (targetShape.m_type & m_disableType) { return false; }
	bool isHit = false;

	for (auto& collisionShape : m_collisionShapes)
	{
		// 用途が一致していない当たり判定形状はスキップ
		if (!(targetShape.m_type & collisionShape.second->GetType())) { continue; }

		KdCollider::CollisionResult tmpRes;
		KdCollider::CollisionResult* pTmpRes = pResults ? &tmpRes : nullptr;

		if (collisionShape.second->Intersects(targetShape, ownerMatrix, pTmpRes))
		{
			isHit = true;

			// 詳細な衝突結果を必要としない場合は1つでも接触して返す
			if (!pResults) { break; }

			pResults->push_back(tmpRes);
		}
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 任意のCollisionShapeを検索して有効/無効を切り替える
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdCollider::SetEnable(std::string_view name, bool flag)
{
	auto targetCol = m_collisionShapes.find(name.data());

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

	DirectX::BoundingSphere myShape;

	m_shape.Transform(myShape, world);

	// 球同士の当たり判定
	bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// お互いに当たらない最小距離
		float needDistance = target.Radius + myShape.Radius;

		// 自身から相手に向かう方向ベクトル
		pRes->m_hitDir = (Math::Vector3(target.Center) - Math::Vector3(myShape.Center));
		float betweenDistance = pRes->m_hitDir.Length();

		// 重なり量 = お互い当たらない最小距離 - お互いの実際距離
		pRes->m_overlapDistance = needDistance - betweenDistance;

		pRes->m_hitDir.Normalize();

		// 当たった座標はお互いの球の衝突の中心点
		pRes->m_hitPos = myShape.Center + pRes->m_hitDir * (myShape.Radius + pRes->m_overlapDistance * 0.5f);
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

	DirectX::BoundingSphere myShape;

	m_shape.Transform(myShape, world);

	// 球vsBOXの当たり判定
	bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// 点をOBBのローカル座標系へ変換(これでOBBからAABBの判定にできる)
		Math::Vector3 _pointCenter = myShape.Center - Math::Vector3(target.Center);

		// BOXの最近接点を求める
		Math::Vector3 _outPos = { 0,0,0 };
		for (int i = 0; i < 3; i++)
		{
			float dist = (&_pointCenter.x)[i];
			if ((&_pointCenter.x)[i] > (&target.Extents.x)[i])
			{
				dist = (&target.Extents.x)[i];
			}
			else if (dist < -(&target.Extents.x)[i])
			{
				dist = -(&target.Extents.x)[i];
			}
			(&_outPos.x)[i] += dist;
		}
		_outPos += target.Center;

		// 自身から相手に向かう方向ベクトル
		pRes->m_hitDir = (_outPos - Math::Vector3(myShape.Center));
		float betweenDistance = pRes->m_hitDir.Length();

		// 重なり量 = お互い当たらない最小距離 - お互いの実際距離
		pRes->m_overlapDistance = myShape.Radius - betweenDistance;

		pRes->m_hitDir.Normalize();
		pRes->m_hitPos = myShape.Center + pRes->m_hitDir * (myShape.Radius + pRes->m_overlapDistance * 0.5f);
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

	DirectX::BoundingSphere myShape;

	m_shape.Transform(myShape, world);

	// 球vsBOXの当たり判定
	bool isHit = myShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// OBBの回転(クォータニオン)
		DirectX::XMFLOAT4 obbQuat = target.Orientation;

		// 点をOBBのローカル座標系へ変換(これでOBBからAABBの判定にできる)
		Math::Vector3 _pointCenter = XMVector3InverseRotate(myShape.Center - Math::Vector3(target.Center), Math::Vector4(obbQuat));

		// BOXの最近接点を求める
		Math::Vector3 _outPos = { 0,0,0 };
		for (int i = 0; i < 3; i++)
		{
			float dist = (&_pointCenter.x)[i];
			if ((&_pointCenter.x)[i] > (&target.Extents.x)[i])
			{
				dist = (&target.Extents.x)[i];
			}
			else if (dist < -(&target.Extents.x)[i])
			{
				dist = -(&target.Extents.x)[i];
			}
			(&_outPos.x)[i] += dist;
		}
		// OBBのローカル座標系からワールドへ戻す
		_outPos = XMVector3Rotate(_outPos, Math::Vector4(obbQuat));
		_outPos += target.Center;

		// 自身から相手に向かう方向ベクトル
		pRes->m_hitDir = (_outPos - Math::Vector3(myShape.Center));
		float betweenDistance = pRes->m_hitDir.Length();

		// 重なり量 = お互い当たらない最小距離 - お互いの実際距離
		pRes->m_overlapDistance = myShape.Radius - betweenDistance;

		pRes->m_hitDir.Normalize();
		pRes->m_hitPos = myShape.Center + pRes->m_hitDir * (myShape.Radius + pRes->m_overlapDistance * 0.5f);
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

	pRes->m_hitPos = tmpRes.m_hitPos;
	pRes->m_hitDir = tmpRes.m_hitDir * -1.0f;
	pRes->m_hitNDir = tmpRes.m_hitNDir * -1.0f;
	pRes->m_overlapDistance = tmpRes.m_overlapDistance;

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

	DirectX::BoundingBox			myAABBShape;
	DirectX::BoundingOrientedBox	myOBBShape;
	m_Abox.Transform(myAABBShape, world);
	m_Obox.Transform(myOBBShape, world);

	DirectX::XMFLOAT4				obbQuat = Math::Vector4::Zero;
	Math::Vector3					myShapeCenter = (!m_IsOriented) ? myAABBShape.Center : myOBBShape.Center;

	// 球vsBOXの当たり判定
	bool isHit = (!m_IsOriented) ? myAABBShape.Intersects(target) : myOBBShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		// 点をOBBのローカル座標系へ変換(これでOBBからAABBの判定にできる)
		Math::Vector3 _pointCenter = Math::Vector3::Zero;
		if (!m_IsOriented)
		{
			_pointCenter = myShapeCenter - Math::Vector3(target.Center);
		}
		else
		{
			// OBBの回転(クォータニオン)
			obbQuat = myOBBShape.Orientation;
			_pointCenter = XMVector3InverseRotate(myShapeCenter - Math::Vector3(target.Center), Math::Vector4(obbQuat));
		}

		// BOXの最近接点を求める
		Math::Vector3 _outPos = { 0,0,0 };
		Math::Vector3 _myExtents = (!m_IsOriented) ? myAABBShape.Extents : myOBBShape.Extents;
		for (int i = 0; i < 3; i++)
		{
			float dist = (&_pointCenter.x)[i];
			if ((&_pointCenter.x)[i] > (&_myExtents.x)[i])
			{
				dist = (&_myExtents.x)[i];
			}
			else if (dist < -(&_myExtents.x)[i])
			{
				dist = -(&_myExtents.x)[i];
			}
			(&_outPos.x)[i] += dist;
		}
		// OBBのローカル座標系からワールドへ戻す
		if (m_IsOriented)_outPos = XMVector3Rotate(_outPos, Math::Vector4(obbQuat));
		_outPos += target.Center;

		// 自身から相手に向かう方向ベクトル
		pRes->m_hitDir = (_outPos - Math::Vector3(myShapeCenter));
		float betweenDistance = pRes->m_hitDir.Length();

		// 重なり量 = お互い当たらない最小距離 - お互いの実際距離
		pRes->m_overlapDistance = target.Radius - betweenDistance;

		pRes->m_hitDir.Normalize();
		pRes->m_hitPos = myShapeCenter + pRes->m_hitDir * (target.Radius + pRes->m_overlapDistance * 0.5f);
	}

	return isHit;
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsBOX(AABB)の当たり判定
// AABBを対象にした判定だが、登録側BOXは AABB / OBB のどちらも来るため
// 詳細リザルトが必要な時は両者を OBB として揃え、SAT で押し出し方向を求める
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const DirectX::BoundingBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// BOXvsBOX(AABB)の当たり判定
	DirectX::BoundingBox			myAABBShape;
	DirectX::BoundingOrientedBox	myOBBShape;
	m_Abox.Transform(myAABBShape, world);
	m_Obox.Transform(myOBBShape, world);

	bool isHit = (!m_IsOriented) ? myAABBShape.Intersects(target) : myOBBShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たっていないなら押し出し情報は不要
	if (!isHit) { return false; }

	// SAT(Separating Axis Theorem) で最小分離軸を求めるため、
	// まずは登録側 / 対象側の両方を OBB 形式へ揃える。
	DirectX::BoundingOrientedBox myBox;
	if (!m_IsOriented)
	{
		DirectX::BoundingOrientedBox::CreateFromBoundingBox(myBox, myAABBShape);
	}
	else
	{
		myBox = myOBBShape;
	}

	DirectX::BoundingOrientedBox targetBox;
	DirectX::BoundingOrientedBox::CreateFromBoundingBox(targetBox, target);

	// OBBのローカル軸をワールド空間へ起こしておく。
	// AABBを OBB 化した対象側は単位軸になるため、このまま両者を同じ式で扱える。
	auto BuildBoxAxes = [](const DirectX::BoundingOrientedBox& box, Math::Vector3 outAxes[3])
	{
		Math::Vector4 quat = Math::Vector4(box.Orientation);
		outAxes[0] = Math::Vector3(XMVector3Rotate(Math::Vector3(1.0f, 0.0f, 0.0f), quat));
		outAxes[1] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 1.0f, 0.0f), quat));
		outAxes[2] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 0.0f, 1.0f), quat));

		outAxes[0].Normalize();
		outAxes[1].Normalize();
		outAxes[2].Normalize();
	};

	// 押し出し軸上の最前面点を取るためのサポート点計算。
	// hitPos は厳密な接触面全体ではなく、「最も押し出しを説明しやすい代表点」として扱う。
	auto GetSupportPoint = [](const DirectX::BoundingOrientedBox& box, const Math::Vector3 axes[3], const Math::Vector3& dir)
	{
		Math::Vector3 support = box.Center;
		const float extents[3] = { box.Extents.x, box.Extents.y, box.Extents.z };

		for (int i = 0; i < 3; i++)
		{
			float sign = (DirectX::XMVector3Dot(dir, axes[i]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;
			support += axes[i] * extents[i] * sign;
		}

		return support;
	};

	Math::Vector3 myAxes[3];
	Math::Vector3 targetAxes[3];
	BuildBoxAxes(myBox, myAxes);
	BuildBoxAxes(targetBox, targetAxes);

	const float myExtents[3] = { myBox.Extents.x, myBox.Extents.y, myBox.Extents.z };
	const float targetExtents[3] = { targetBox.Extents.x, targetBox.Extents.y, targetBox.Extents.z };

	Math::Vector3 centerDelta = Math::Vector3(targetBox.Center) - Math::Vector3(myBox.Center);

	// SAT では、相手の中心差を登録側BOXのローカル軸へ投影して扱うと式が揃う。
	float t[3] =
	{
		DirectX::XMVector3Dot(centerDelta, myAxes[0]).m128_f32[0],
		DirectX::XMVector3Dot(centerDelta, myAxes[1]).m128_f32[0],
		DirectX::XMVector3Dot(centerDelta, myAxes[2]).m128_f32[0]
	};

	float rot[3][3] = {};
	float absRot[3][3] = {};
	constexpr float kSatEpsilon = 0.0001f;

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			rot[i][j] = DirectX::XMVector3Dot(myAxes[i], targetAxes[j]).m128_f32[0];
			absRot[i][j] = fabsf(rot[i][j]) + kSatEpsilon;
		}
	}

	// 15本の分離軸候補の中から、最も浅い重なり量を持つ軸を採用する。
	// これが「相手BOXを最短で外へ出せる方向」になる。
	float minOverlap = FLT_MAX;
	Math::Vector3 bestAxis = Math::Vector3::Zero;

	auto UpdateBestAxis = [&](Math::Vector3 axis, float overlap)
	{
		if (overlap < 0.0f)
		{
			// DirectX の Intersects とわずかに誤差が出ても暴れにくいよう、
			// ごく小さい負値は 0 とみなして続行する。
			if (overlap < -kSatEpsilon) { return false; }
			overlap = 0.0f;
		}

		if (axis.LengthSquared() <= kSatEpsilon)
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

	// 1. 登録側BOXの面法線3本を調べる
	for (int i = 0; i < 3; i++)
	{
		float ra = myExtents[i];
		float rb =
			targetExtents[0] * absRot[i][0] +
			targetExtents[1] * absRot[i][1] +
			targetExtents[2] * absRot[i][2];

		float dist = fabsf(t[i]);
		float overlap = ra + rb - dist;
		float sign = (t[i] >= 0.0f) ? 1.0f : -1.0f;

		if (!UpdateBestAxis(myAxes[i] * sign, overlap)) { return false; }
	}

	// 2. 対象側BOXの面法線3本を調べる
	for (int j = 0; j < 3; j++)
	{
		float ra =
			myExtents[0] * absRot[0][j] +
			myExtents[1] * absRot[1][j] +
			myExtents[2] * absRot[2][j];
		float rb = targetExtents[j];

		float projectedCenter =
			t[0] * rot[0][j] +
			t[1] * rot[1][j] +
			t[2] * rot[2][j];
		float dist = fabsf(projectedCenter);
		float overlap = ra + rb - dist;
		float sign = (DirectX::XMVector3Dot(centerDelta, targetAxes[j]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;

		if (!UpdateBestAxis(targetAxes[j] * sign, overlap)) { return false; }
	}

	// 3. 辺同士で押し合うケースもあるため、外積でできる9本の軸も調べる
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			Math::Vector3 axis = myAxes[i].Cross(targetAxes[j]);
			if (axis.LengthSquared() <= kSatEpsilon)
			{
				// 軸がほぼ平行なら、この外積軸は意味を持たないので飛ばす
				continue;
			}

			float ra =
				myExtents[(i + 1) % 3] * absRot[(i + 2) % 3][j] +
				myExtents[(i + 2) % 3] * absRot[(i + 1) % 3][j];
			float rb =
				targetExtents[(j + 1) % 3] * absRot[i][(j + 2) % 3] +
				targetExtents[(j + 2) % 3] * absRot[i][(j + 1) % 3];

			float dist = fabsf(
				t[(i + 2) % 3] * rot[(i + 1) % 3][j] -
				t[(i + 1) % 3] * rot[(i + 2) % 3][j]
			);
			float overlap = ra + rb - dist;
			float sign = (DirectX::XMVector3Dot(centerDelta, axis).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;

			if (!UpdateBestAxis(axis * sign, overlap)) { return false; }
		}
	}

	// 採用軸は「登録側BOX -> 対象BOX」の向きで返す。
	// Character 側はこの向きへ overlapDistance ぶん動かすことで分離できる。
	pRes->m_hitDir = NormalizeOrFallback(bestAxis, centerDelta, world.Right(), world.Up());
	pRes->m_overlapDistance = minOverlap;

	// 接触位置は、互いに向かい合うサポート点同士の中点を代表値として返す。
	Math::Vector3 mySupport = GetSupportPoint(myBox, myAxes, pRes->m_hitDir);
	Math::Vector3 targetSupport = GetSupportPoint(targetBox, targetAxes, -pRes->m_hitDir);
	pRes->m_hitPos = (mySupport + targetSupport) * 0.5f;
	pRes->m_hitNDir = pRes->m_hitDir;

	return true;
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsBOX(OBB)の当たり判定
// 対象側が回転付きBOXなので、詳細リザルトが必要な時は
// 登録側BOXも OBB に揃えて SAT で最小分離軸を求める
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable) { return false; }

	// BOXvsBOX(OBB)の当たり判定
	DirectX::BoundingBox			myAABBShape;
	DirectX::BoundingOrientedBox	myOBBShape;
	m_Abox.Transform(myAABBShape, world);
	m_Obox.Transform(myOBBShape, world);

	bool isHit = (!m_IsOriented) ? myAABBShape.Intersects(target) : myOBBShape.Intersects(target);

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たっていないなら押し出し情報は不要
	if (!isHit) { return false; }

	// SAT(Separating Axis Theorem) で最小分離軸を求めるため、
	// まずは登録側を OBB 形式へ揃える。
	DirectX::BoundingOrientedBox myBox;
	if (!m_IsOriented)
	{
		DirectX::BoundingOrientedBox::CreateFromBoundingBox(myBox, myAABBShape);
	}
	else
	{
		myBox = myOBBShape;
	}

	// 対象側はすでに OBB なのでそのまま使える
	const DirectX::BoundingOrientedBox& targetBox = target;

	// OBB のローカル軸をワールド空間へ変換する。
	// この3軸を使って、面法線6本 + 外積軸9本の計15軸を調べる。
	auto BuildBoxAxes = [](const DirectX::BoundingOrientedBox& box, Math::Vector3 outAxes[3])
	{
		Math::Vector4 quat = Math::Vector4(box.Orientation);
		outAxes[0] = Math::Vector3(XMVector3Rotate(Math::Vector3(1.0f, 0.0f, 0.0f), quat));
		outAxes[1] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 1.0f, 0.0f), quat));
		outAxes[2] = Math::Vector3(XMVector3Rotate(Math::Vector3(0.0f, 0.0f, 1.0f), quat));

		outAxes[0].Normalize();
		outAxes[1].Normalize();
		outAxes[2].Normalize();
	};

	// 押し出し方向に対して各BOXの最前面点を取る補助計算。
	// hitPos は接触面全体の厳密解ではなく、押し出しを説明しやすい代表点として返す。
	auto GetSupportPoint = [](const DirectX::BoundingOrientedBox& box, const Math::Vector3 axes[3], const Math::Vector3& dir)
	{
		Math::Vector3 support = box.Center;
		const float extents[3] = { box.Extents.x, box.Extents.y, box.Extents.z };

		for (int i = 0; i < 3; i++)
		{
			float sign = (DirectX::XMVector3Dot(dir, axes[i]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;
			support += axes[i] * extents[i] * sign;
		}

		return support;
	};

	Math::Vector3 myAxes[3];
	Math::Vector3 targetAxes[3];
	BuildBoxAxes(myBox, myAxes);
	BuildBoxAxes(targetBox, targetAxes);

	const float myExtents[3] = { myBox.Extents.x, myBox.Extents.y, myBox.Extents.z };
	const float targetExtents[3] = { targetBox.Extents.x, targetBox.Extents.y, targetBox.Extents.z };

	Math::Vector3 centerDelta = Math::Vector3(targetBox.Center) - Math::Vector3(myBox.Center);

	// 中心差を登録側BOXの軸へ投影しておくと、各分離軸の式を統一して書ける
	float t[3] =
	{
		DirectX::XMVector3Dot(centerDelta, myAxes[0]).m128_f32[0],
		DirectX::XMVector3Dot(centerDelta, myAxes[1]).m128_f32[0],
		DirectX::XMVector3Dot(centerDelta, myAxes[2]).m128_f32[0]
	};

	float rot[3][3] = {};
	float absRot[3][3] = {};
	constexpr float kSatEpsilon = 0.0001f;

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			rot[i][j] = DirectX::XMVector3Dot(myAxes[i], targetAxes[j]).m128_f32[0];
			absRot[i][j] = fabsf(rot[i][j]) + kSatEpsilon;
		}
	}

	// 15本の分離軸候補の中から、最も浅い重なり量を持つ軸を選ぶ。
	// これが相手BOXを最短で分離できる押し出し方向になる。
	float minOverlap = FLT_MAX;
	Math::Vector3 bestAxis = Math::Vector3::Zero;

	auto UpdateBestAxis = [&](Math::Vector3 axis, float overlap)
	{
		if (overlap < 0.0f)
		{
			// DirectX の Intersects と数値誤差が出ても暴れにくいよう、
			// ごく小さい負値だけは 0 扱いで続行する。
			if (overlap < -kSatEpsilon) { return false; }
			overlap = 0.0f;
		}

		if (axis.LengthSquared() <= kSatEpsilon)
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

	// 1. 登録側BOXの面法線3本を調べる
	for (int i = 0; i < 3; i++)
	{
		float ra = myExtents[i];
		float rb =
			targetExtents[0] * absRot[i][0] +
			targetExtents[1] * absRot[i][1] +
			targetExtents[2] * absRot[i][2];

		float dist = fabsf(t[i]);
		float overlap = ra + rb - dist;
		float sign = (t[i] >= 0.0f) ? 1.0f : -1.0f;

		if (!UpdateBestAxis(myAxes[i] * sign, overlap)) { return false; }
	}

	// 2. 対象側BOXの面法線3本を調べる
	for (int j = 0; j < 3; j++)
	{
		float ra =
			myExtents[0] * absRot[0][j] +
			myExtents[1] * absRot[1][j] +
			myExtents[2] * absRot[2][j];
		float rb = targetExtents[j];

		float projectedCenter =
			t[0] * rot[0][j] +
			t[1] * rot[1][j] +
			t[2] * rot[2][j];
		float dist = fabsf(projectedCenter);
		float overlap = ra + rb - dist;
		float sign = (DirectX::XMVector3Dot(centerDelta, targetAxes[j]).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;

		if (!UpdateBestAxis(targetAxes[j] * sign, overlap)) { return false; }
	}

	// 3. 辺同士が最初に当たるケースもあるため、外積でできる9本の軸も調べる
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			Math::Vector3 axis = myAxes[i].Cross(targetAxes[j]);
			if (axis.LengthSquared() <= kSatEpsilon)
			{
				// ほぼ平行な軸同士の外積は有効な分離軸にならない
				continue;
			}

			float ra =
				myExtents[(i + 1) % 3] * absRot[(i + 2) % 3][j] +
				myExtents[(i + 2) % 3] * absRot[(i + 1) % 3][j];
			float rb =
				targetExtents[(j + 1) % 3] * absRot[i][(j + 2) % 3] +
				targetExtents[(j + 2) % 3] * absRot[i][(j + 1) % 3];

			float dist = fabsf(
				t[(i + 2) % 3] * rot[(i + 1) % 3][j] -
				t[(i + 1) % 3] * rot[(i + 2) % 3][j]
			);
			float overlap = ra + rb - dist;
			float sign = (DirectX::XMVector3Dot(centerDelta, axis).m128_f32[0] >= 0.0f) ? 1.0f : -1.0f;

			if (!UpdateBestAxis(axis * sign, overlap)) { return false; }
		}
	}

	// 採用軸は「登録側BOX -> 対象BOX」の向きで返す。
	// 呼び出し側はこの向きに overlapDistance ぶん動かすことで最短分離できる。
	pRes->m_hitDir = NormalizeOrFallback(bestAxis, centerDelta, world.Right(), world.Up());
	pRes->m_overlapDistance = minOverlap;

	// 接触位置は、互いに向かい合うサポート点の中点を代表値として返す
	Math::Vector3 mySupport = GetSupportPoint(myBox, myAxes, pRes->m_hitDir);
	Math::Vector3 targetSupport = GetSupportPoint(targetBox, targetAxes, -pRes->m_hitDir);
	pRes->m_hitPos = (mySupport + targetSupport) * 0.5f;
	pRes->m_hitNDir = pRes->m_hitDir;

	return true;
}
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// BOXvsレイの当たり判定
// 判定回数は 1 回　計算自体も軽く最も軽量な当たり判定　計算回数も固定なので処理効率は安定
// 片方の球の判定を0にすれば単純な距離判定も作れる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdBoxCollision::Intersects(const KdCollider::RayInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* /*pRes*/)
{
	if (!m_enable) { return false; }

	// AABB vs レイ
	float AABBdist = FLT_MAX;

	// BOXvsBOX(OBB)の当たり判定
	DirectX::BoundingBox			myAABBShape;
	DirectX::BoundingOrientedBox	myOBBShape;
	m_Abox.Transform(myAABBShape, world);
	m_Obox.Transform(myOBBShape, world);
	bool isHit = (!m_IsOriented) ? myAABBShape.Intersects(target.m_pos, target.m_dir, AABBdist) : myOBBShape.Intersects(target.m_pos, target.m_dir, AABBdist);

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

	// BOX側は「target のカプセルをどう押し戻すか」を返したいので、
	// 実際の判定はカプセル側実装へ委譲し、向きだけカプセル用に反転して返す。
	DirectX::BoundingBox			myAABBShape;
	DirectX::BoundingOrientedBox	myOBBShape;
	m_Abox.Transform(myAABBShape, world);
	m_Obox.Transform(myOBBShape, world);

	KdCapsuleCollision capsuleShape(target);

	// まずは当たったかどうかだけ欲しい場合
	if (!pRes)
	{
		return (!m_IsOriented) ?
			capsuleShape.Intersects(myAABBShape, Math::Matrix::Identity, nullptr) :
			capsuleShape.Intersects(myOBBShape, Math::Matrix::Identity, nullptr);
	}

	KdCollider::CollisionResult tmpRes;
	bool isHit = (!m_IsOriented) ?
		capsuleShape.Intersects(myAABBShape, Math::Matrix::Identity, &tmpRes) :
		capsuleShape.Intersects(myOBBShape, Math::Matrix::Identity, &tmpRes);

	if (!isHit) { return false; }

	// カプセル側実装は「BOXを押し戻す向き」で結果を返すため、
	// BOX側から見る時はカプセルを押し戻せる向きへ反転する。
	pRes->m_hitPos = tmpRes.m_hitPos;
	pRes->m_hitDir = tmpRes.m_hitDir * -1.0f;
	pRes->m_hitNDir = tmpRes.m_hitNDir * -1.0f;
	pRes->m_overlapDistance = tmpRes.m_overlapDistance;

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

	// 各メッシュに押される用の球・押される毎に座標を更新する必要がある
	DirectX::BoundingSphere pushedSphere = target;
	// 計算用にFloat3 → Vectorへ変換
	Math::Vector3 pushedSphereCenter = DirectX::XMLoadFloat3(&pushedSphere.Center);

	bool isHit = false;

	Math::Vector3 hitPos;
	Math::Vector3 hitNDir;

	// 当たり判定ノードとのみ当たり判定
	for (int index : spModelData->GetCollisionMeshNodeIndices())
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
bool KdModelCollision::Intersects(const DirectX::BoundingBox& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	// TODO: 当たり計算は各自必要に応じて拡張して下さい
	return false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// モデルvsBOX(OBB)の当たり判定
// 判定回数は メッシュの個数 x 各メッシュのポリゴン数 計算回数がモデルのデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdModelCollision::Intersects(const DirectX::BoundingOrientedBox& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	// TODO: 当たり計算は各自必要に応じて拡張して下さい
	return false;
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

	CollisionMeshResult nearestResult;

	bool isHit = false;

	const std::vector<KdModelData::Node>& dataNodes = spModelData->GetOriginalNodes();
	const std::vector<KdModelWork::Node>& workNodes = m_shape->GetNodes();

	for (int index : spModelData->GetCollisionMeshNodeIndices())
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

		isHit = true;

		if (tmpResult.m_overlapDistance > nearestResult.m_overlapDistance)
		{
			nearestResult = tmpResult;
		}
	}

	if (pRes && isHit)
	{
		// 最も近くで当たったヒット情報をコピーする
		pRes->m_hitPos = nearestResult.m_hitPos;

		pRes->m_hitDir = nearestResult.m_hitDir;

		pRes->m_overlapDistance = nearestResult.m_overlapDistance;

		// 最も近くで当たった面の法線が使用される
		pRes->m_hitNDir = nearestResult.m_hitNDir;
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
	Math::Vector3 capsuleCenter = target.m_pos + target.m_offset;

	float capsuleRadius = target.m_radius;
	if (capsuleRadius < 0.0f)
	{
		capsuleRadius = 0.0f;
	}

	float capsuleHeight = target.m_height;
	if (capsuleHeight < 0.0f)
	{
		capsuleHeight = 0.0f;
	}
	if (capsuleHeight < capsuleRadius * 2.0f)
	{
		capsuleHeight = capsuleRadius * 2.0f;
	}

	float cylinderLength = capsuleHeight - capsuleRadius * 2.0f;
	Math::Vector3 capsuleStart = capsuleCenter - Math::Vector3::Up * (cylinderLength * 0.5f);
	Math::Vector3 capsuleEnd = capsuleCenter + Math::Vector3::Up * (cylinderLength * 0.5f);

	// 球の間隔は半径以下にして、見た目のカプセルに近い当たり形状を作る
	int sampleCount = 1;
	if (cylinderLength > kCapsuleEpsilon)
	{
		// サンプル間隔が広いと、球と球の間の輪郭が痩せて
		// BOXの面に対して見た目上めり込みやすくなる。
		// そのため半径の半分間隔でサンプルし、前後方向の当たりも安定させる。
		float sampleStep = capsuleRadius * 0.5f;
		if (sampleStep <= kCapsuleEpsilon)
		{
			sampleStep = cylinderLength;
		}

		sampleCount = static_cast<int>(std::ceil(cylinderLength / sampleStep)) + 1;
		sampleCount = std::clamp(sampleCount, 2, 16);
	}

	std::vector<Math::Vector3> sampleCenters;
	sampleCenters.reserve(sampleCount);

	if (sampleCount == 1)
	{
		sampleCenters.push_back(capsuleCenter);
	}
	else
	{
		Math::Vector3 capsuleAxis = capsuleEnd - capsuleStart;
		for (int i = 0; i < sampleCount; i++)
		{
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			sampleCenters.push_back(capsuleStart + capsuleAxis * t);
		}
	}

	// カプセル全体を包む球を作っておくと、メッシュ単位の粗い早期除外に使える
	DirectX::BoundingSphere broadSphere;
	broadSphere.Center = capsuleCenter;
	broadSphere.Radius = capsuleRadius + cylinderLength * 0.5f;

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

	// 1回で全押し戻しを加算すると、
	// 同じ壁に触れている複数サンプル球が補正を重複させてしまいガタつきやすい。
	// そこで「今の状態で最も強い押し戻し」を1回だけ採用し、必要なら数回繰り返す。
	constexpr int kSolveIteration = 4;
	for (int solve = 0; solve < kSolveIteration; solve++)
	{
		bool hitThisSolve = false;
		Math::Vector3 bestPush = Math::Vector3::Zero;
		Math::Vector3 bestHitPos = Math::Vector3::Zero;
		Math::Vector3 bestHitNDir = Math::Vector3::Zero;

		for (int index : spModelData->GetCollisionMeshNodeIndices())
		{
			const KdModelData::Node& dataNode = dataNodes[index];
			const KdModelWork::Node& workNode = workNodes[index];

			// あり得ないはずだが一応チェック
			if (!dataNode.m_spMesh) { continue; }

			const Math::Matrix meshWorld = workNode.m_worldTransform * world;

			// メッシュ単位のブロードフェイズ
			DirectX::BoundingSphere currentBroadSphere = broadSphere;
			currentBroadSphere.Center = Math::Vector3(currentBroadSphere.Center) + totalPush;

			DirectX::BoundingBox meshAabb;
			dataNode.m_spMesh->GetBoundingBox().Transform(meshAabb, meshWorld);
			if (!meshAabb.Intersects(currentBroadSphere))
			{
				continue;
			}

			// カプセルを構成する各球で詳細判定する
			for (const Math::Vector3& baseCenter : sampleCenters)
			{
				DirectX::BoundingSphere sampleSphere;
				sampleSphere.Center = baseCenter + totalPush;
				sampleSphere.Radius = capsuleRadius;

				CollisionMeshResult tmpResult;
				CollisionMeshResult* pTmpResult = pRes ? &tmpResult : nullptr;

				if (!MeshIntersect(*dataNode.m_spMesh, sampleSphere, meshWorld, pTmpResult))
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
		if (!hitThisSolve || bestPush.LengthSquared() <= kCapsuleEpsilon)
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

	// メッシュと球形の当たり判定実行
	if (!PolygonsIntersect(*m_shape, target, world, pTmpResult))
	{
		// 当たっていなければ無条件に返る
		return false;
	}

	if (pRes)
	{
		pRes->m_hitPos = result.m_hitPos;

		pRes->m_hitDir = result.m_hitDir;

		pRes->m_overlapDistance = result.m_overlapDistance;

		pRes->m_hitNDir = result.m_hitNDir;
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

	if (!PolygonsIntersect(*m_shape, target.m_pos, target.m_dir, target.m_range, world, pTmpResult))
	{
		// 当たっていなければ無条件に返る
		return false;
	}

	if (pRes)
	{
		pRes->m_hitPos = result.m_hitPos;

		pRes->m_hitDir = result.m_hitDir;

		pRes->m_overlapDistance = result.m_overlapDistance;

		pRes->m_hitNDir = result.m_hitNDir;
	}

	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 多角形ポリゴン(頂点の集合体)vsカプセルの当たり判定
// 判定回数は ポリゴンの個数 計算回数がポリゴンデータ依存のため処理効率は不安定
// 単純に計算回数が多くなる可能性があるため重くなりがち
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdPolygonCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
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

	Math::Vector3 pos			= m_shape->m_pos + m_shape->m_offset;
	Math::Vector3 capsuleCenter = Math::Vector3::Transform(pos, world);
	Math::Vector3 up = world.Up();
	float upScale = up.Length();
	if (upScale <= kCapsuleEpsilon)
	{
		up = Math::Vector3::Up;
		upScale = 1.0f;
	}
	else
	{
		up.Normalize();
	}

	float radiusScale = world.Right().Length();
	float backwardScale = world.Backward().Length();
	if (backwardScale > radiusScale)
	{
		radiusScale = backwardScale;
	}
	if (radiusScale <= kCapsuleEpsilon)
	{
		radiusScale = 1.0f;
	}

	float capsuleRadius = m_shape->m_radius;
	if (capsuleRadius < 0.0f)
	{
		capsuleRadius = 0.0f;
	}
	capsuleRadius *= radiusScale;

	float capsuleHeight = m_shape->m_height;
	if (capsuleHeight < 0.0f)
	{
		capsuleHeight = 0.0f;
	}
	capsuleHeight *= upScale;
	if (capsuleHeight < capsuleRadius * 2.0f)
	{
		capsuleHeight = capsuleRadius * 2.0f;
	}

	float cylinderLength = capsuleHeight - capsuleRadius * 2.0f;
	Math::Vector3 capsuleStart = capsuleCenter - up * (cylinderLength * 0.5f);
	Math::Vector3 capsuleEnd = capsuleCenter + up * (cylinderLength * 0.5f);

	Math::Vector3 closestPos = ClosestPointOnSegment(Math::Vector3(target.Center), capsuleStart, capsuleEnd);
	Math::Vector3 capsuleToTarget = Math::Vector3(target.Center) - closestPos;

	float needDistance = capsuleRadius + target.Radius;
	bool isHit = capsuleToTarget.LengthSquared() <= needDistance * needDistance;

	// 詳細リザルトが必要無ければ即結果を返す
	if (!pRes) { return isHit; }

	// 当たった時のみ計算
	if (isHit)
	{
		float betweenDistance = capsuleToTarget.Length();

		pRes->m_hitDir = NormalizeOrFallback(
			capsuleToTarget,
			Math::Vector3(target.Center) - capsuleCenter,
			world.Right(),
			up
		);

		pRes->m_overlapDistance = needDistance - betweenDistance;

		pRes->m_hitPos = closestPos + pRes->m_hitDir * (capsuleRadius + pRes->m_overlapDistance * 0.5f);

		pRes->m_hitNDir = pRes->m_hitDir;
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

	// まずは登録側カプセルを「中心線分 + 半径」に変換する
	Math::Vector3 pos = m_shape->m_pos + m_shape->m_offset;
	Math::Vector3 capsuleCenter = Math::Vector3::Transform(pos, world);

	// 行列の Up 方向をカプセルの縦軸として使う
	Math::Vector3 up = world.Up();
	float upScale = up.Length();
	if (upScale <= kCapsuleEpsilon)
	{
		up = Math::Vector3::Up;
		upScale = 1.0f;
	}
	else
	{
		up.Normalize();
	}

	// 半径は横方向スケールの大きい方を採用して潰れを防ぐ
	float radiusScale = world.Right().Length();
	float backwardScale = world.Backward().Length();
	if (backwardScale > radiusScale)
	{
		radiusScale = backwardScale;
	}
	if (radiusScale <= kCapsuleEpsilon)
	{
		radiusScale = 1.0f;
	}

	float capsuleRadius = m_shape->m_radius;
	if (capsuleRadius < 0.0f)
	{
		capsuleRadius = 0.0f;
	}
	capsuleRadius *= radiusScale;

	float capsuleHeight = m_shape->m_height;
	if (capsuleHeight < 0.0f)
	{
		capsuleHeight = 0.0f;
	}
	capsuleHeight *= upScale;
	if (capsuleHeight < capsuleRadius * 2.0f)
	{
		// 高さが直径より小さい場合は球として扱える最小サイズに丸める
		capsuleHeight = capsuleRadius * 2.0f;
	}

	float cylinderLength = capsuleHeight - capsuleRadius * 2.0f;
	Math::Vector3 capsuleStart = capsuleCenter - up * (cylinderLength * 0.5f);
	Math::Vector3 capsuleEnd = capsuleCenter + up * (cylinderLength * 0.5f);

	// AABBはカプセル専用の低レベル判定を持っていないため、
	// ここではカプセルを縦方向に並んだ複数の球として扱い、各球とAABBの最近接点を使って判定する。
	int sampleCount = 1;
	if (cylinderLength > kCapsuleEpsilon)
	{
		// サンプル間隔が広いと、球と球の間の輪郭が痩せて
		// BOXの面に対して見た目上めり込みやすくなる。
		// そのため半径の半分間隔でサンプルし、前後方向の当たりも安定させる。
		float sampleStep = capsuleRadius * 0.5f;
		if (sampleStep <= kCapsuleEpsilon)
		{
			sampleStep = cylinderLength;
		}

		sampleCount = static_cast<int>(std::ceil(cylinderLength / sampleStep)) + 1;
		sampleCount = std::clamp(sampleCount, 2, 16);
	}

	std::vector<Math::Vector3> sampleCenters;
	sampleCenters.reserve(sampleCount);

	if (sampleCount == 1)
	{
		sampleCenters.push_back(capsuleCenter);
	}
	else
	{
		Math::Vector3 capsuleAxis = capsuleEnd - capsuleStart;
		for (int i = 0; i < sampleCount; i++)
		{
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			sampleCenters.push_back(capsuleStart + capsuleAxis * t);
		}
	}

	// target の AABB は「当たる側」の情報なので、呼び出し時点で既にワールド座標系にある。
	// ここでさらに world を掛けると、登録側カプセルの行列を相手BOXへ二重適用してしまい、
	// 面の位置がずれて前後方向の押し戻し量が狂う原因になる。
	const Math::Vector3 boxMin = Math::Vector3(target.Center) - Math::Vector3(target.Extents);
	const Math::Vector3 boxMax = Math::Vector3(target.Center) + Math::Vector3(target.Extents);

	// 粗い早期除外用に、カプセル全体を包む球を使う
	DirectX::BoundingSphere broadSphere;
	broadSphere.Center = capsuleCenter;
	broadSphere.Radius = capsuleRadius + cylinderLength * 0.5f;
	if (!target.Intersects(broadSphere))
	{
		return false;
	}

	bool isHit = false;

	// 複数のサンプル球が同じ面に同時に触れると補正を重複させやすいので、
	// 1回の解決では最も強い押し戻しだけを採用し、必要なら数回だけ再解決する。
	Math::Vector3 totalCorrection = Math::Vector3::Zero;
	Math::Vector3 hitPos = Math::Vector3::Zero;
	Math::Vector3 hitNDir = Math::Vector3::Zero;

	constexpr int kSolveIteration = 4;
	for (int solve = 0; solve < kSolveIteration; solve++)
	{
		bool hitThisSolve = false;
		Math::Vector3 bestCorrection = Math::Vector3::Zero;
		Math::Vector3 bestHitPos = Math::Vector3::Zero;
		Math::Vector3 bestHitNDir = Math::Vector3::Zero;

		for (const Math::Vector3& baseCenter : sampleCenters)
		{
			// サンプル球は、これまでに求めた実際の補正量だけ動かした位置で再判定する
			Math::Vector3 sphereCenter = baseCenter + totalCorrection;

			// まずは球の中心がAABBの内側か外側かを調べる
			bool isInside =
				(sphereCenter.x >= boxMin.x && sphereCenter.x <= boxMax.x) &&
				(sphereCenter.y >= boxMin.y && sphereCenter.y <= boxMax.y) &&
				(sphereCenter.z >= boxMin.z && sphereCenter.z <= boxMax.z);

			Math::Vector3 nearestPos = sphereCenter;
			// hitDir は「カプセル -> BOX」の向きで揃える。
			// ただし実際にカプセルを押し出す時は、この逆向きを使う。
			Math::Vector3 hitDir = Math::Vector3::Zero;
			float overlapDistance = 0.0f;

			if (!isInside)
			{
				// 外側にいる場合は、AABB上の最近接点との距離で球vsAABB判定をする
				nearestPos.x = std::clamp(sphereCenter.x, boxMin.x, boxMax.x);
				nearestPos.y = std::clamp(sphereCenter.y, boxMin.y, boxMax.y);
				nearestPos.z = std::clamp(sphereCenter.z, boxMin.z, boxMax.z);

				Math::Vector3 toBox = nearestPos - sphereCenter;
				float distSqr = toBox.LengthSquared();
				if (distSqr > capsuleRadius * capsuleRadius)
				{
					continue;
				}

				float betweenDistance = std::sqrt(distSqr);
				hitDir = NormalizeOrFallback(
					toBox,
					Math::Vector3(target.Center) - sphereCenter,
					world.Right(),
					up
				);

				overlapDistance = capsuleRadius - betweenDistance;
			}
			else
			{
				// 内部にいる場合は、最も近い面までの距離を調べてそこへ押し出す
				float distToMinX = sphereCenter.x - boxMin.x;
				float distToMaxX = boxMax.x - sphereCenter.x;
				float distToMinY = sphereCenter.y - boxMin.y;
				float distToMaxY = boxMax.y - sphereCenter.y;
				float distToMinZ = sphereCenter.z - boxMin.z;
				float distToMaxZ = boxMax.z - sphereCenter.z;

				float nearestFaceDist = distToMinX;
				hitDir = Math::Vector3(-1.0f, 0.0f, 0.0f);
				nearestPos = Math::Vector3(boxMin.x, sphereCenter.y, sphereCenter.z);

				if (distToMaxX < nearestFaceDist)
				{
					nearestFaceDist = distToMaxX;
					hitDir = Math::Vector3(1.0f, 0.0f, 0.0f);
					nearestPos = Math::Vector3(boxMax.x, sphereCenter.y, sphereCenter.z);
				}
				if (distToMinY < nearestFaceDist)
				{
					nearestFaceDist = distToMinY;
					hitDir = Math::Vector3(0.0f, -1.0f, 0.0f);
					nearestPos = Math::Vector3(sphereCenter.x, boxMin.y, sphereCenter.z);
				}
				if (distToMaxY < nearestFaceDist)
				{
					nearestFaceDist = distToMaxY;
					hitDir = Math::Vector3(0.0f, 1.0f, 0.0f);
					nearestPos = Math::Vector3(sphereCenter.x, boxMax.y, sphereCenter.z);
				}
				if (distToMinZ < nearestFaceDist)
				{
					nearestFaceDist = distToMinZ;
					hitDir = Math::Vector3(0.0f, 0.0f, -1.0f);
					nearestPos = Math::Vector3(sphereCenter.x, sphereCenter.y, boxMin.z);
				}
				if (distToMaxZ < nearestFaceDist)
				{
					nearestFaceDist = distToMaxZ;
					hitDir = Math::Vector3(0.0f, 0.0f, 1.0f);
					nearestPos = Math::Vector3(sphereCenter.x, sphereCenter.y, boxMax.z);
				}

				// 球の中心が面まで食い込んでいるぶん + 半径ぶんだけ押し戻せば分離できる
				overlapDistance = capsuleRadius + nearestFaceDist;
			}

			// 詳細リザルトが必要無ければ、1つでも当たった時点で返す
			if (!pRes) { return true; }

			hitThisSolve = true;
			isHit = true;

			// 返却規約上の hitDir は BOX へ向かう向きなので、
			// 実際にカプセルを分離させる補正は逆向きに取る。
			Math::Vector3 correction = hitDir * -overlapDistance;

			// 今回の解決で最も強い押し戻しだけを採用する
			if (correction.LengthSquared() > bestCorrection.LengthSquared())
			{
				bestCorrection = correction;
				bestHitPos = sphereCenter + hitDir * (capsuleRadius + overlapDistance * 0.5f);
				bestHitNDir = hitDir;
			}
		}

		// これ以上押し出しが必要なければ終了
		if (!hitThisSolve || bestCorrection.LengthSquared() <= kCapsuleEpsilon)
		{
			break;
		}

		totalCorrection += bestCorrection;
		hitPos = bestHitPos;
		hitNDir = bestHitNDir;
	}

	if (pRes && isHit)
	{
		// 実際の補正は BOX -> カプセル方向なので、
		// 返却時は既存実装の規約に合わせて反転し「カプセル -> BOX」で返す。
		pRes->m_overlapDistance = totalCorrection.Length();
		pRes->m_hitDir = NormalizeOrFallback(-totalCorrection, hitNDir, world.Right(), up);

		pRes->m_hitPos = hitPos;
		pRes->m_hitNDir = hitNDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsBOX(OBB)の当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const DirectX::BoundingOrientedBox& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	// 当たり判定が無効 or 形状が解放済みなら判定せず返る
	if (!m_enable || !m_shape) { return false; }

	// まずは登録側カプセルを「中心線分 + 半径」に変換する
	Math::Vector3 pos = m_shape->m_pos + m_shape->m_offset;
	Math::Vector3 capsuleCenter = Math::Vector3::Transform(pos, world);

	// 行列の Up 方向をカプセルの縦軸として使う
	Math::Vector3 up = world.Up();
	float upScale = up.Length();
	if (upScale <= kCapsuleEpsilon)
	{
		up = Math::Vector3::Up;
		upScale = 1.0f;
	}
	else
	{
		up.Normalize();
	}

	// 半径は横方向スケールの大きい方を採用して潰れを防ぐ
	float radiusScale = world.Right().Length();
	float backwardScale = world.Backward().Length();
	if (backwardScale > radiusScale)
	{
		radiusScale = backwardScale;
	}
	if (radiusScale <= kCapsuleEpsilon)
	{
		radiusScale = 1.0f;
	}

	float capsuleRadius = m_shape->m_radius;
	if (capsuleRadius < 0.0f)
	{
		capsuleRadius = 0.0f;
	}
	capsuleRadius *= radiusScale;

	float capsuleHeight = m_shape->m_height;
	if (capsuleHeight < 0.0f)
	{
		capsuleHeight = 0.0f;
	}
	capsuleHeight *= upScale;
	if (capsuleHeight < capsuleRadius * 2.0f)
	{
		// 高さが直径より小さい場合は球として扱える最小サイズに丸める
		capsuleHeight = capsuleRadius * 2.0f;
	}

	float cylinderLength = capsuleHeight - capsuleRadius * 2.0f;
	Math::Vector3 capsuleStart = capsuleCenter - up * (cylinderLength * 0.5f);
	Math::Vector3 capsuleEnd = capsuleCenter + up * (cylinderLength * 0.5f);

	// OBBも低レベルのカプセル判定を持たないため、
	// カプセルを複数球へ分解し、各球を OBB のローカル座標系で AABB として判定する。
	int sampleCount = 1;
	if (cylinderLength > kCapsuleEpsilon)
	{
		// OBB相手でも、サンプル間隔が広いと球と球の間の輪郭が痩せて
		// 面に対して前後方向のめり込みが見えやすくなる。
		// AABB版と同じく半径の半分間隔まで細かくして、押し戻し量を安定させる。
		float sampleStep = capsuleRadius * 0.5f;
		if (sampleStep <= kCapsuleEpsilon)
		{
			sampleStep = cylinderLength;
		}

		sampleCount = static_cast<int>(std::ceil(cylinderLength / sampleStep)) + 1;
		sampleCount = std::clamp(sampleCount, 2, 16);
	}

	std::vector<Math::Vector3> sampleCenters;
	sampleCenters.reserve(sampleCount);

	if (sampleCount == 1)
	{
		sampleCenters.push_back(capsuleCenter);
	}
	else
	{
		Math::Vector3 capsuleAxis = capsuleEnd - capsuleStart;
		for (int i = 0; i < sampleCount; i++)
		{
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			sampleCenters.push_back(capsuleStart + capsuleAxis * t);
		}
	}

	// 粗い早期除外用に、カプセル全体を包む球を使う
	DirectX::BoundingSphere broadSphere;
	broadSphere.Center = capsuleCenter;
	broadSphere.Radius = capsuleRadius + cylinderLength * 0.5f;
	if (!target.Intersects(broadSphere))
	{
		return false;
	}

	const Math::Vector3 boxExtents = target.Extents;
	const Math::Vector4 obbQuat = Math::Vector4(target.Orientation);

	bool isHit = false;

	// 複数サンプルの補正をそのまま全部足すとガタつきやすいので、
	// 1回の解決では最も強い押し戻しだけを採用し、必要なら数回だけ再解決する。
	// OBB版も考え方は AABB版と同じで、
	// 返却規約用の向きと実際の補正向きが逆になる点に注意する。
	Math::Vector3 totalCorrection = Math::Vector3::Zero;
	Math::Vector3 hitPos = Math::Vector3::Zero;
	Math::Vector3 hitNDir = Math::Vector3::Zero;

	constexpr int kSolveIteration = 4;
	for (int solve = 0; solve < kSolveIteration; solve++)
	{
		bool hitThisSolve = false;
		Math::Vector3 bestCorrection = Math::Vector3::Zero;
		Math::Vector3 bestHitPos = Math::Vector3::Zero;
		Math::Vector3 bestHitNDir = Math::Vector3::Zero;

		for (const Math::Vector3& baseCenter : sampleCenters)
		{
			Math::Vector3 sphereCenter = baseCenter + totalCorrection;

			// OBBのローカル空間へ変換すると、回転付きBOXも AABB として扱える
			Math::Vector3 localCenter = XMVector3InverseRotate(
				sphereCenter - Math::Vector3(target.Center),
				obbQuat
			);

			bool isInside =
				(localCenter.x >= -boxExtents.x && localCenter.x <= boxExtents.x) &&
				(localCenter.y >= -boxExtents.y && localCenter.y <= boxExtents.y) &&
				(localCenter.z >= -boxExtents.z && localCenter.z <= boxExtents.z);

			Math::Vector3 nearestLocal = localCenter;
			Math::Vector3 hitDir = Math::Vector3::Zero;
			float overlapDistance = 0.0f;

			if (!isInside)
			{
				// 外側にいる場合は、OBB上の最近接点との距離で判定する
				nearestLocal.x = std::clamp(localCenter.x, -boxExtents.x, boxExtents.x);
				nearestLocal.y = std::clamp(localCenter.y, -boxExtents.y, boxExtents.y);
				nearestLocal.z = std::clamp(localCenter.z, -boxExtents.z, boxExtents.z);

				Math::Vector3 nearestWorld = Math::Vector3(XMVector3Rotate(nearestLocal, obbQuat)) + Math::Vector3(target.Center);
				Math::Vector3 toBox = nearestWorld - sphereCenter;
				float distSqr = toBox.LengthSquared();
				if (distSqr > capsuleRadius * capsuleRadius)
				{
					continue;
				}

				float betweenDistance = std::sqrt(distSqr);
				hitDir = NormalizeOrFallback(
					toBox,
					Math::Vector3(target.Center) - sphereCenter,
					world.Right(),
					up
				);

				overlapDistance = capsuleRadius - betweenDistance;
			}
			else
			{
				// 内部にいる場合は、最も近い面の法線方向へ押し戻せば分離できる
				float distToMinX = localCenter.x + boxExtents.x;
				float distToMaxX = boxExtents.x - localCenter.x;
				float distToMinY = localCenter.y + boxExtents.y;
				float distToMaxY = boxExtents.y - localCenter.y;
				float distToMinZ = localCenter.z + boxExtents.z;
				float distToMaxZ = boxExtents.z - localCenter.z;

				float nearestFaceDist = distToMinX;
				Math::Vector3 hitDirLocal(-1.0f, 0.0f, 0.0f);

				if (distToMaxX < nearestFaceDist)
				{
					nearestFaceDist = distToMaxX;
					hitDirLocal = Math::Vector3(1.0f, 0.0f, 0.0f);
				}
				if (distToMinY < nearestFaceDist)
				{
					nearestFaceDist = distToMinY;
					hitDirLocal = Math::Vector3(0.0f, -1.0f, 0.0f);
				}
				if (distToMaxY < nearestFaceDist)
				{
					nearestFaceDist = distToMaxY;
					hitDirLocal = Math::Vector3(0.0f, 1.0f, 0.0f);
				}
				if (distToMinZ < nearestFaceDist)
				{
					nearestFaceDist = distToMinZ;
					hitDirLocal = Math::Vector3(0.0f, 0.0f, -1.0f);
				}
				if (distToMaxZ < nearestFaceDist)
				{
					nearestFaceDist = distToMaxZ;
					hitDirLocal = Math::Vector3(0.0f, 0.0f, 1.0f);
				}

				hitDir = XMVector3Rotate(hitDirLocal, obbQuat);
				overlapDistance = capsuleRadius + nearestFaceDist;
			}

			// 詳細リザルトが必要無ければ、1つでも当たった時点で返す
			if (!pRes) { return true; }

			hitThisSolve = true;
			isHit = true;

			Math::Vector3 correction = hitDir * -overlapDistance;

			// 今回の解決で最も強い押し戻しだけを採用する
			if (correction.LengthSquared() > bestCorrection.LengthSquared())
			{
				bestCorrection = correction;
				bestHitPos = sphereCenter + hitDir * (capsuleRadius + overlapDistance * 0.5f);
				bestHitNDir = hitDir;
			}
		}

		// これ以上押し出しが必要なければ終了
		if (!hitThisSolve || bestCorrection.LengthSquared() <= kCapsuleEpsilon)
		{
			break;
		}

		totalCorrection += bestCorrection;
		hitPos = bestHitPos;
		hitNDir = bestHitNDir;
	}

	if (pRes && isHit)
	{
		pRes->m_overlapDistance = totalCorrection.Length();
		pRes->m_hitDir = NormalizeOrFallback(-totalCorrection, hitNDir, world.Right(), up);

		pRes->m_hitPos = hitPos;
		pRes->m_hitNDir = hitNDir;
	}

	return isHit;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsレイの当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const KdCollider::RayInfo& /*target*/, const Math::Matrix& /*world*/, KdCollider::CollisionResult* /*pRes*/)
{
	if (!m_enable) { return false; }

	return false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// カプセルvsカプセルの当たり判定
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdCapsuleCollision::Intersects(const KdCollider::CapsuleInfo& target, const Math::Matrix& world, KdCollider::CollisionResult* pRes)
{
	if (!m_enable || !m_shape) { return false; }

	// まずは登録側カプセルを「中心線分 + 半径」に変換する
	Math::Vector3 myPos = m_shape->m_pos + m_shape->m_offset;
	Math::Vector3 myCenter = Math::Vector3::Transform(myPos, world);

	// 行列の Up 方向をカプセルの縦軸として使う
	Math::Vector3 myUp = world.Up();
	float myUpScale = myUp.Length();
	if (myUpScale <= kCapsuleEpsilon)
	{
		myUp = Math::Vector3::Up;
		myUpScale = 1.0f;
	}
	else
	{
		myUp.Normalize();
	}

	// 半径は横方向スケールの大きい方を採用して潰れを防ぐ
	float myRadiusScale = world.Right().Length();
	float myBackwardScale = world.Backward().Length();
	if (myBackwardScale > myRadiusScale)
	{
		myRadiusScale = myBackwardScale;
	}
	if (myRadiusScale <= kCapsuleEpsilon)
	{
		myRadiusScale = 1.0f;
	}

	float myRadius = m_shape->m_radius;
	if (myRadius < 0.0f)
	{
		myRadius = 0.0f;
	}
	myRadius *= myRadiusScale;

	float myHeight = m_shape->m_height;
	if (myHeight < 0.0f)
	{
		myHeight = 0.0f;
	}
	myHeight *= myUpScale;
	if (myHeight < myRadius * 2.0f)
	{
		// 高さが直径より小さい場合は球として扱える最小サイズに丸める
		myHeight = myRadius * 2.0f;
	}

	float myCylinderLength = myHeight - myRadius * 2.0f;
	Math::Vector3 myStart = myCenter - myUp * (myCylinderLength * 0.5f);
	Math::Vector3 myEnd = myCenter + myUp * (myCylinderLength * 0.5f);

	// 対象側も同様に「中心線分 + 半径」へ変換する
	Math::Vector3 targetCenter = target.m_pos + target.m_offset;
	// CapsuleInfo だけでは回転情報を持てないので対象側はワールドUp基準で扱う
	Math::Vector3 targetUp = Math::Vector3::Up;

	float targetRadius = target.m_radius;
	if (targetRadius < 0.0f)
	{
		targetRadius = 0.0f;
	}

	float targetHeight = target.m_height;
	if (targetHeight < 0.0f)
	{
		targetHeight = 0.0f;
	}
	if (targetHeight < targetRadius * 2.0f)
	{
		targetHeight = targetRadius * 2.0f;
	}

	float targetCylinderLength = targetHeight - targetRadius * 2.0f;
	Math::Vector3 targetStart = targetCenter - targetUp * (targetCylinderLength * 0.5f);
	Math::Vector3 targetEnd = targetCenter + targetUp * (targetCylinderLength * 0.5f);

	Math::Vector3 d1 = myEnd - myStart;
	Math::Vector3 d2 = targetEnd - targetStart;
	Math::Vector3 r = myStart - targetStart;

	float a = DirectX::XMVector3Dot(d1, d1).m128_f32[0];
	float e = DirectX::XMVector3Dot(d2, d2).m128_f32[0];
	float f = DirectX::XMVector3Dot(d2, r).m128_f32[0];

	// 2本の線分上で最も近くなる位置 s,t を求める
	float s = 0.0f;
	float t = 0.0f;

	if (a <= kCapsuleEpsilon && e <= kCapsuleEpsilon)
	{
		// 両方ほぼ点
		s = 0.0f;
		t = 0.0f;
	}
	else if (a <= kCapsuleEpsilon)
	{
		// 自分側だけほぼ点
		s = 0.0f;
		t = std::clamp(f / e, 0.0f, 1.0f);
	}
	else
	{
		float c = DirectX::XMVector3Dot(d1, r).m128_f32[0];

		if (e <= kCapsuleEpsilon)
		{
			// 対象側だけほぼ点
			t = 0.0f;
			s = std::clamp(-c / a, 0.0f, 1.0f);
		}
		else
		{
			// 一般的な線分同士。無限直線で最短になる位置を出してから線分範囲へ丸める
			float b = DirectX::XMVector3Dot(d1, d2).m128_f32[0];
			float denom = a * e - b * b;

			if (fabsf(denom) > kCapsuleEpsilon)
			{
				s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
			}
			else
			{
				// ほぼ平行なら片方の始点寄せから始める
				s = 0.0f;
			}

			t = (b * s + f) / e;

			if (t < 0.0f)
			{
				// t が範囲外なら対象側始点に合わせて再計算
				t = 0.0f;
				s = std::clamp(-c / a, 0.0f, 1.0f);
			}
			else if (t > 1.0f)
			{
				// t が範囲外なら対象側終点に合わせて再計算
				t = 1.0f;
				s = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	// 最短距離になる2点間ベクトルが、そのまま押し出し方向の元になる
	Math::Vector3 myClosest = myStart + d1 * s;
	Math::Vector3 targetClosest = targetStart + d2 * t;
	Math::Vector3 hitVec = targetClosest - myClosest;

	float needDistance = myRadius + targetRadius;
	bool isHit = hitVec.LengthSquared() <= needDistance * needDistance;

	if (!pRes) { return isHit; }

	if (isHit)
	{
		float betweenDistance = hitVec.Length();

		// ベクトルが潰れていても押し出し方向が失われないようフォールバックを用意する
		pRes->m_hitDir = NormalizeOrFallback(
			hitVec,
			targetCenter - myCenter,
			world.Right(),
			myUp
		);

		// 必要距離との差分が重なり量
		pRes->m_overlapDistance = needDistance - betweenDistance;
		// 接触位置は登録側カプセル表面と対象側カプセル表面の中間点
		pRes->m_hitPos = myClosest + pRes->m_hitDir * (myRadius + pRes->m_overlapDistance * 0.5f);
		pRes->m_hitNDir = pRes->m_hitDir;
	}

	return isHit;
}
