//======================================================================================
//	Ed Kurlyak 2023 BSP Tree Software Triangle Rasterization
//======================================================================================

#ifndef _LEVEL_LOADER_
#define _LEVEL_LOADER_

#include <windows.h>
#include <math.h>
#include <stdio.h>

#include <vfw.h> //��� DrawDibDraw
#pragma comment(lib, "Vfw32.lib") //��� DrawDibDraw

#define PI 3.14159265358979f

#define BITS_PER_PIXEL	32

enum {COINCIDENT, IN_BACK_OF, IN_FRONT_OF, SPANNING};

struct vector4
{
	vector4();
	vector4(float ix, float iy, float iz);
	~vector4();

	float x = 0.0f;
	float y = 0.0f; 
	float z = 0.0f;
	float w = 1.0f;
	
	float tu = 0.0f;
	float tv = 0.0f;

	vector4& operator = (const vector4& VecIn);
	
	//vector subtract
	vector4 operator - (const vector4& VecIn);

	//vector scale
	vector4 operator * (const float& ValIn);

	//vector add
	vector4 operator + (const vector4& VecIn);

	//vector add
	vector4 &operator += (const vector4& VecIn);
};

struct polygon;

struct plane
{
	float a = 0.0f;
	float b = 0.0f;
	float c = 0.0f;
	float d = 0.0f;

	int Classify_Polygon (polygon *Poly);
	float Classify_Point (vector4 Eye);
	vector4 Normal();
};

struct polygon
{
	polygon(){};
	~polygon(){};
	
	vector4 Vertex[3];
	int  TexID = 0;

	vector4 GetNormal();
	plane Get_Plane();
};

struct list 
{
	list()
	{
		PolygonCurr = 0; PolygonCount = 0; PolyList = NULL;
	};

	~list()
	{
		if (PolyList != NULL)
		{
			free(PolyList);
			PolyList = NULL;
		}
		PolygonCurr = 0;
		PolygonCount = 0;

	}

	void Reset()
	{
		if (PolyList != NULL)
		{
			free(PolyList);
			PolyList = NULL;
		}
		PolygonCurr = 0;
		PolygonCount = 0;

	}

	//����������� ����������� ��� �� ����������
	//list ��� �������� �������
	list(const list& l)
	{
		PolyList = NULL;
		PolygonCount = 0;
		PolygonCurr = 0;

		PolygonCount = l.PolygonCount;
		PolygonCurr = l.PolygonCurr;
		PolyList = (polygon*)malloc(PolygonCount * sizeof(polygon));

		memcpy(&PolyList[0], &l.PolyList[0], PolygonCount * sizeof(polygon));

	}

	//�������� ������������ ������������
	list& operator = (list& l)
	{
		if (PolyList != NULL)
		{
			delete[] PolyList;
			PolygonCount = 0;
			PolygonCurr = 0;
		}

		PolygonCount = l.PolygonCount;
		PolygonCurr = l.PolygonCurr;

		PolyList = (polygon*)malloc(PolygonCount * sizeof(polygon));

		memcpy(&PolyList[0], &l.PolyList[0], PolygonCount * sizeof(polygon));

		return *this;
	}

	bool Is_Empty_List ();
	polygon *Get_From_List();
	void Add_To_List(polygon *p);
	
	polygon* PolyList = NULL;
	UINT PolygonCount = 0;
	UINT PolygonCurr = 0;
};

//matrix offset
enum {
	M00, M01, M02, M03,
	M10, M11, M12, M13,
	M20, M21, M22, M23,
	M30, M31, M32, M33
};

struct matrix4x4
{
	float Mat[16];
	
	matrix4x4 ();
	~matrix4x4 ();
	
	matrix4x4(float ir1c1, float ir1c2, float ir1c3, float ir1c4,
		float ir2c1, float ir2c2, float ir2c3, float ir2c4,
		float ir3c1, float ir3c2, float ir3c3, float ir3c4,
		float ir4c1, float ir4c2, float ir4c3, float ir4c4);
	
};

struct	BSP_tree
{
	BSP_tree() { Front = NULL; Back = NULL; };
	~BSP_tree()
	{
		if (Front != NULL)
		{
			delete Front;
			Front = NULL;
		}

		if (Back != NULL)
		{
			delete Back;
			Back = NULL;
		}
	};
	
	plane Partition;
	list Polygons;
	list TransformedPolygons;
	BSP_tree *Front = NULL, *Back = NULL;
};

struct texture_info
{
	UINT TexWidth = 0;
	UINT TexHeight = 0;
};

class CMeshManager
{
public:

	CMeshManager();
	~CMeshManager();

	void Init_MeshManager(HWND hWnd);
	void Update_MeshManager();
	void Draw_MeshManager();

	void Create_BackBuffer();
	void Clear_BackBuffer();
	void Present_BackBuffer();
	void Delete_BackBuffer();

	vector4 Vec4_Normalize(vector4& VecIn);
	matrix4x4 Matrix_Rotation_Axis(vector4 &VecIn, float Angle);
	vector4 Calc_Edge(vector4 &VecIn1, vector4 &VecIn2);
	vector4 Vec4_Mat4x4_Mul(vector4& VecIn, matrix4x4& MatIn);
	matrix4x4 Mat4x4_Mat4x4_Mul(matrix4x4& MatIn1, matrix4x4& MatIn2);
	float Vec4_Dot(vector4& VecIn1, vector4& VecIn2);
	vector4 Vec4_Cross(vector4& VecIn1, vector4& VecIn2);

	int Get_TextureID(char * TexName);

	bool Load_BMP(char *Filename, int Tile);

	void Build_BSP_Tree(BSP_tree *Tree, list Polygons);
	void Split_Polygon(polygon *Poly, plane *Part, polygon *&Front, polygon *&Back, int &cf, int &cb);
	void Delete_BSP(BSP_tree *Tree);
	void Transform_BSP_Tree(BSP_tree *Tree);
	void Draw_BSP_Tree(BSP_tree *Tree, vector4 Eye);

	void Extract_Frustum();
	bool Polygon_In_Frustum(UINT NumPoints, vector4* PointList);

	void Draw_Polygon_List(list Polygons);
	void Draw_Textured_Triangle(vector4 VecIn1, vector4 VecIn2, vector4 VecIn3);
	void Draw_Textured_Poly(int y1, int y2);

	void Timer_Start();
	float Get_Elapsed_Time();

	matrix4x4 Get_View_Matrix();
	
	UINT m_ViewWidth = 0;
	UINT m_ViewHeight = 0;
	
	HWND m_hWnd = NULL;

	vector4 m_VecRight = vector4(0.0f, 0.0f, 0.0f);
	vector4 m_VecUp = vector4(0.0f, 0.0f, 0.0f);
	vector4 m_VecLook = vector4(0.0f, 0.0f, 0.0f);
	vector4 m_VecPos = vector4(0.0f, 0.0f, 0.0f);

	list m_Polygons;

	BSP_tree* m_Root = NULL;

	matrix4x4 m_MatWorld;
	matrix4x4 m_MatView;
	matrix4x4 m_MatProj;
	matrix4x4 m_MatRes;

	float m_Frustum[6][4] = { 0 };
	
	float m_dxdyl = 0.0f, m_dudyl = 0.0f,
		m_dvdyl = 0.0f, m_dzdyl = 0.0f,
		m_dxdyr = 0.0f, m_dudyr = 0.0f,
		m_dvdyr = 0.0f, m_dzdyr = 0.0f;
	float m_ul = 0.0f, m_ur = 0.0f,
		m_vl = 0.0f, m_vr = 0.0f,
		m_zl = 0.0f, m_zr = 0.0f,
		m_xl = 0.0f, m_xr = 0.0f;

	//������� ��������
	UCHAR *m_Tex = NULL;
	texture_info *m_TexInfo = NULL;
	//������ ���� ������� �����
	unsigned char** m_LevelTile = NULL;
	UINT m_TextureWidth = 0;
	UINT m_TextureHeight = 0;


	//������� ������ �����
	UINT m_BackLpitch = 0;
	LPBYTE m_Data = NULL;
	LPBYTE m_DataTemp = NULL;
	BITMAPINFOHEADER m_Bih = { 0 };
	HDRAWDIB m_hDD = NULL;

	//timer
	float m_ElapsedTime = 0.0f;
	float m_TimeScale = 0.0f;
	__int64 m_StartTime = 0;
	__int64 m_LastTime = 0;
	__int64 m_PerfFreq = 0;

	float m_ZNear = 0.0f;
};



#endif