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
	//�������˳��ֱ�Ϊǰ���������£�
	D3DXVECTOR4 planes[6];
	//v1->v3˳��ֱ�Ϊ������棨frontside���������£����ϣ����ϡ�
	void GetPlane(D3DXVECTOR3 v1, D3DXVECTOR3 v2, D3DXVECTOR3 v3, D3DXVECTOR4* plane);
public:
	Frustum(float xmin, float xmax, float ymin, float ymax, float nearz, float farz);
	FaceSide VertexPlaneSide(int planeId, D3DXVECTOR4 vertex);
};