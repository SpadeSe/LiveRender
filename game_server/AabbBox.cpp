#include "AabbBox.h"
#include <stdlib.h>

#define BOX_MAX 1e14

AabbBox::AabbBox(){
	minP.x = BOX_MAX;
	minP.y = BOX_MAX;
	minP.z = BOX_MAX;

	maxP.x = -BOX_MAX;
	maxP.y = -BOX_MAX;
	maxP.z = -BOX_MAX;
}

AabbBox::AabbBox(vector<Vertex> vertexs)
{
	AabbBox();
	for(int i = 0; i < vertexs.size();i++){
		AddP(vertexs[i]);
	}
}

void AabbBox::AddP(Vertex P){
	minP.x = min(minP.x, P.x);
	minP.y = min(minP.y, P.y);
	minP.z = min(minP.z, P.z);

	maxP.x = max(maxP.x, P.x);
	maxP.y = max(maxP.y, P.y);
	maxP.z = max(maxP.z, P.z);

	XMFLOAT3 pos(P.x, P.y, P.z);
	XMVECTOR posV = XMLoadFloat3(&pos);
	XMVECTOR minV = XMLoadFloat3(&minPos);
	XMVECTOR maxV = XMLoadFloat3(&maxPos);
	maxV = XMVectorMax(maxV, posV);
	minV = XMVectorMin(minV, posV);
	XMStoreFloat3(&minPos, minV);
	XMStoreFloat3(&maxPos, maxV);
}

void AabbBox::WriteToLog()
{
	Log::log("AABB Box:\n");
	Log::log_notime("\tmin:( %f, %f, %f )\n\tmax:( %f, %f, %f )\n", minP.x, minP.y, minP.z, maxP.x, maxP.y, maxP.z);
	Log::log_notime("\tmin:( %f, %f, %f )\n\tmax:( %f, %f, %f )\n", minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
}

void AabbBox::GetAllPoints(D3DXVECTOR4* outpoints)
{
	outpoints[0] = D3DXVECTOR4(maxP.x, minP.y, minP.z, 1);
	outpoints[1] = D3DXVECTOR4(maxP.x, minP.y, maxP.z, 1);
	outpoints[2] = D3DXVECTOR4(maxP.x, maxP.y, maxP.z, 1);
	outpoints[3] = D3DXVECTOR4(maxP.x, maxP.y, minP.z, 1);
	outpoints[4] = D3DXVECTOR4(minP.x, minP.y, minP.z, 1);
	outpoints[5] = D3DXVECTOR4(minP.x, minP.y, maxP.z, 1);
	outpoints[6] = D3DXVECTOR4(minP.x, maxP.y, maxP.z, 1);
	outpoints[7] = D3DXVECTOR4(minP.x, maxP.y, minP.z, 1);

}

