#define SSPR_MIRROR_MAX 16

Texture2D g_depthTex : register(t0);
// CS は色そのものではなく、どの元画面画素を採用するかだけを packed して書き込む。
RWTexture2D<uint> g_projectionTex : register(u0);

cbuffer cbSSPR : register(b1)
{
	row_major float4x4 g_mView;
	row_major float4x4 g_mProj;
	row_major float4x4 g_mProjInv;
	row_major float4x4 g_mViewInv;

	float2 g_screenSize;
	float2 g_invScreenSize;
	int g_mirrorCount;
	float3 g_blank0;

	float4 g_planePosStrength[SSPR_MIRROR_MAX];
	float4 g_planeNormalBias[SSPR_MIRROR_MAX];
	float4 g_planeRightHalfWidth[SSPR_MIRROR_MAX];
	float4 g_planeUpHalfHeight[SSPR_MIRROR_MAX];
	float4 g_planeParams[SSPR_MIRROR_MAX];
};

float3 GetPlanePos(int mirrorIdx) { return g_planePosStrength[mirrorIdx].xyz; }
float3 GetPlaneNormal(int mirrorIdx) { return g_planeNormalBias[mirrorIdx].xyz; }
float GetPlaneBias(int mirrorIdx) { return g_planeNormalBias[mirrorIdx].w; }
float3 GetPlaneRight(int mirrorIdx) { return g_planeRightHalfWidth[mirrorIdx].xyz; }
float GetHalfWidth(int mirrorIdx) { return g_planeRightHalfWidth[mirrorIdx].w; }
float3 GetPlaneUp(int mirrorIdx) { return g_planeUpHalfHeight[mirrorIdx].xyz; }
float GetHalfHeight(int mirrorIdx) { return g_planeUpHalfHeight[mirrorIdx].w; }

float3 ReconstructWorldPos(float2 uv, float depth)
{
	float2 projXY = (uv * 2.0f - 1.0f) * float2(1.0f, -1.0f);
	float4 viewPos = mul(float4(projXY, depth, 1.0f), g_mProjInv);
	viewPos.xyz /= viewPos.w;

	float4 worldPos = mul(float4(viewPos.xyz, 1.0f), g_mViewInv);
	return worldPos.xyz;
}

bool IsMirrorSurface(float3 worldPos, int mirrorIdx)
{
	float3 fromPlane = worldPos - GetPlanePos(mirrorIdx);
	float planeDistance = abs(dot(fromPlane, GetPlaneNormal(mirrorIdx)));
	if (planeDistance > GetPlaneBias(mirrorIdx))
	{
		return false;
	}

	float rightDistance = abs(dot(fromPlane, GetPlaneRight(mirrorIdx)));
	float upDistance = abs(dot(fromPlane, GetPlaneUp(mirrorIdx)));

	return rightDistance <= GetHalfWidth(mirrorIdx) && upDistance <= GetHalfHeight(mirrorIdx);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
	if (dispatchId.x >= (uint)g_screenSize.x || dispatchId.y >= (uint)g_screenSize.y)
	{
		return;
	}

	float depth = g_depthTex.Load(int3(dispatchId.xy, 0)).r;
	if (depth >= 1.0f)
	{
		return;
	}

	float2 srcUV = (float2(dispatchId.xy) + 0.5f) * g_invScreenSize;
	float3 srcWorldPos = ReconstructWorldPos(srcUV, depth);

	for (int mirrorIdx = 0; mirrorIdx < g_mirrorCount; ++mirrorIdx)
	{
		float3 planePos = GetPlanePos(mirrorIdx);
		float3 planeNormal = GetPlaneNormal(mirrorIdx);
		float planeBias = GetPlaneBias(mirrorIdx);

		// 鏡面そのものの画素は投影元にしないで、鏡の向こう側の景色だけを拾う。
		float planeSide = dot(srcWorldPos - planePos, planeNormal);
		if (abs(planeSide) <= planeBias)
		{
			continue;
		}

		float3 mirroredWorldPos = srcWorldPos - 2.0f * planeSide * planeNormal;

		float4 clipPos = mul(mul(float4(mirroredWorldPos, 1.0f), g_mView), g_mProj);
		if (clipPos.w <= 0.0f)
		{
			continue;
		}

		float3 ndcPos = clipPos.xyz / clipPos.w;
		float2 destUV = ndcPos.xy * float2(0.5f, -0.5f) + 0.5f;
		if (destUV.x < 0.0f || destUV.x > 1.0f || destUV.y < 0.0f || destUV.y > 1.0f)
		{
			continue;
		}

		int2 destPixel = int2(destUV * g_screenSize);
		if (destPixel.x < 0 || destPixel.y < 0 || destPixel.x >= g_screenSize.x || destPixel.y >= g_screenSize.y)
		{
			continue;
		}

		float destDepth = g_depthTex.Load(int3(destPixel, 0)).r;
		if (destDepth >= 1.0f)
		{
			continue;
		}

		float2 destPixelUV = (float2(destPixel) + 0.5f) * g_invScreenSize;
		float3 destWorldPos = ReconstructWorldPos(destPixelUV, destDepth);
		if (!IsMirrorSurface(destWorldPos, mirrorIdx))
		{
			continue;
		}

		// 上位 8bit に深度、下位 24bit に元画面座標を詰める。
		uint depthBits = (uint)round(saturate(ndcPos.z) * 255.0f);
		uint packedValue = (depthBits << 24) | ((dispatchId.y & 0x0FFF) << 12) | (dispatchId.x & 0x0FFF);

		// 複数候補が同じ鏡面画素へ集まった時は、より手前の反射結果を残す。
		InterlockedMin(g_projectionTex[destPixel], packedValue);
	}
}
