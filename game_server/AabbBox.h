#pragma once
#include "yslim.h"
#include "matrix.h"
#include "utility.h"
#include <math.h>
#include <vector>
#include <d3dx9.h>
#include "DirectXMath.h"

using namespace DirectX;
using std::vector;

//AABB盒（比较简易方便）
class AabbBox{
public:
	Vertex minP;
	Vertex maxP;
	XMFLOAT3 minPos;
	XMFLOAT3 maxPos;

public:
	AabbBox();
	AabbBox(vector<Vertex> vertexs);
	void AddP(Vertex p);
	void WriteToLog();
	void GetAllPoints(D3DXVECTOR4* outpoints);
	//static AabbBox TransformBox(AabbBox* old, Matrix mat);
};