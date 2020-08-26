//#include "Collision.h"
//
//int BoundingBox::corner_count = 8;
//
//BoundingBox::BoundingBox(D3DXVECTOR4 c, D3DXVECTOR4 e)
//{
//	center = c;
//	extends = e;
//}
//
//void BoundingBox::Transform(BoundingBox* out, D3DXMATRIX m)
//{
//	D3DXVec4Transform(&out->center, &center, &m);
//	//D3DXVec4Transform(&out->extends, &extends, &m);
//	D3DXVECTOR4 mr0(m.m[0]);
//	D3DXVECTOR4 mr1(m.m[1]);
//	D3DXVECTOR4 mr2(m.m[2]);
//	out->extends.x = extends.x * D3DXVec4Length(&mr0);
//	out->extends.y = extends.y * D3DXVec4Length(&mr1);
//	out->extends.z = extends.z * D3DXVec4Length(&mr2);
//}
//
//void BoundingBox::GetCorners(D3DXVECTOR4* corners)
//{
//
//}
//
//ContainmentType BoundingBox::Contains(BoundingFrustum* fr)
//{
//	if(!fr->Intersects(this)){
//		return DISJOINT;
//	}
//	/*D3DXVECTOR4 corners[fr->corner_count];
//	fr->GetCorners(corners);
//	for(int i = 0; i < fr->corner_count; i++){
//
//	}*/
//}
//
//int BoundingFrustum::corner_count = 8;
//
//void BoundingFrustum::GetCorners(D3DXVECTOR4* corners)
//{
//
//}
//
//bool BoundingFrustum::Intersects(BoundingBox* bb)
//{
//
//}
//
//bool Vector4InBounds(D3DXVECTOR4* v, D3DXVECTOR4 bounds)
//{
//
//}
