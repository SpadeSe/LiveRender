#include "Frustum.h"

void Frustum::GetPlane(D3DXVECTOR3 v1, D3DXVECTOR3 v2, D3DXVECTOR3 v3, D3DXVECTOR4* plane)
{
	D3DXVECTOR3 faceNormal, crossTemp;
	D3DXVec3Normalize(&faceNormal, D3DXVec3Cross(&crossTemp, &(v2 - v1), &(v3 - v1)));
	faceNormal *= -1;
	plane->x = faceNormal.x;
	plane->y = faceNormal.y;
	plane->z = faceNormal.z;
	plane->w = D3DXVec3Dot(&faceNormal, &v1);
}

Frustum::Frustum(float xmin, float xmax, float ymin, float ymax, float nearz, float farz)
{
	Log::log("Frustum::Frustum:\n\txmin: %f, xmax: %f, ymin: %f, ymax: %f\n\tnear: %f, far: %f\n", 
		xmin, xmax, ymin, ymax, nearz, farz);
	D3DXVECTOR3 LeftTopNear(xmin, ymax, nearz);
	D3DXVECTOR3 RightTopNear(xmax, ymax, nearz);
	D3DXVECTOR3 LeftBotNear(xmin, ymin, nearz);
	D3DXVECTOR3 RightBotNear(xmax, ymin, nearz);
	D3DXVECTOR3 LeftTopFar(xmin * farz / nearz, ymax * farz / nearz, farz);
	D3DXVECTOR3 RightTopFar(xmax * farz / nearz, ymax * farz / nearz, farz);
	D3DXVECTOR3 LeftBotFar(xmin * farz / nearz, ymin * farz / nearz, farz);
	D3DXVECTOR3 RightBotFar(xmax * farz / nearz, ymin * farz / nearz, farz);
	GetPlane(LeftBotNear, LeftTopNear, RightTopNear, &planes[0]);
	GetPlane(RightBotFar, RightTopFar, LeftTopFar, &planes[1]);
	GetPlane(LeftBotFar, LeftTopFar, LeftTopNear, &planes[2]);
	GetPlane(RightBotNear, RightTopNear, RightTopFar, &planes[3]);
	GetPlane(LeftTopNear, LeftTopFar, RightTopFar, &planes[4]);
	GetPlane(LeftBotFar, LeftBotNear, RightBotNear, &planes[5]);
}

FaceSide Frustum::VertexPlaneSide(int planeId, D3DXVECTOR4 vertex)
{
	D3DXVECTOR3 planeNorm(planes[planeId].x, planes[planeId].y, planes[planeId].z);
	D3DXVECTOR3 vertexPos(vertex.x, vertex.y, vertex.z);
	float t = D3DXVec3Dot(&planeNorm, &vertexPos) - planes[planeId].w;
	if(t < 0){
		return BEHIND;
	}
	else if(t == 0){
		return ONFACE;
	}
	else{
		return FRONT;
	}
}

