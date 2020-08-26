#pragma once
#include "utility.h"

#include <d3dx9.h>

enum FaceSide{
	BEHIND,
	ONFACE,
	FRONT
};

class Frustum{
private:
	//六个面的顺序分别为前后左右上下；
	D3DXVECTOR4 planes[6];
	//v1->v3顺序分别为面从正面（frontside）看的左下，左上，右上。
	void GetPlane(D3DXVECTOR3 v1, D3DXVECTOR3 v2, D3DXVECTOR3 v3, D3DXVECTOR4* plane);
public:
	Frustum(float xmin, float xmax, float ymin, float ymax, float nearz, float farz);
	FaceSide VertexPlaneSide(int planeId, D3DXVECTOR4 vertex);
};