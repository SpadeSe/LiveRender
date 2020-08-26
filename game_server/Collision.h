//#pragma once
//#include <stdlib.h>
//#include <d3dx9.h>
//#include "yslim.h"
//
//enum ContainmentType
//{
//	DISJOINT = 0,
//	INTERSECTS = 1,
//	CONTAINS = 2
//};
//
//class BoundingBox;
//class BoundingFrustum;
//
//class BoundingBox{
//private:
//	D3DXVECTOR4 center;
//	D3DXVECTOR4 extends;
//	//D3DXVECTOR4 orientation;
//public:
//	static int corner_count;
//public:
//	BoundingBox(D3DXVECTOR4 c, D3DXVECTOR4 e);
//	void Transform(BoundingBox* out, D3DXMATRIX m);
//	void GetCorners(D3DXVECTOR4* corners);
//	ContainmentType Contains(BoundingFrustum* fr);
//public:
//	//static void CreateFromPoints(BoundingBox* out, Vertex* points, int counts);
//};
//
//class BoundingFrustum{
//private:
//	float Right, Left;            // Negative X
//	float Top, Bottom;          // Negative Y
//	float Near, Far;            // Z of the near plane and far plane.
//public:
//	static int corner_count;
//public:
//	void GetCorners(D3DXVECTOR4* corners);
//	bool Intersects(BoundingBox* bb);
//};
//
//bool Vector4InBounds(D3DXVECTOR4* v, D3DXVECTOR4 bounds);
