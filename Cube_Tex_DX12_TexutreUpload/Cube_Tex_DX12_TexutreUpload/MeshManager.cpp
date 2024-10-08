//======================================================================================
//	Ed Kurlyak 2023 Software Rendering Textured Cube DirectX12 Backbuffer
//======================================================================================

#include "MeshManager.h"

CMeshManager::CMeshManager()
{
}

CMeshManager::~CMeshManager()
{
	if (m_d3dDevice != nullptr)
		FlushCommandQueue();
}

void CMeshManager::EnableDebugLayer_CreateFactory()
{
#if defined(DEBUG) || defined(_DEBUG) 
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));
}

void CMeshManager::Create_Device()
{
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_d3dDevice));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3dDevice)));
	}
}

void CMeshManager::CreateFence_GetDescriptorsSize()
{
	ThrowIfFailed(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&m_Fence)));

	m_RtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvUavDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void CMeshManager::Check_Multisample_Quality()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_d3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
}

void CMeshManager::Create_CommandList_Allocator_Queue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

	ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_DirectCmdListAlloc.GetAddressOf())));

	ThrowIfFailed(m_d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_DirectCmdListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	m_CommandList->Close();

}

void CMeshManager::Create_SwapChain()
{
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_ClientWidth;
	sd.BufferDesc.Height = m_ClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = m_BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = m_SwapChainBufferCount;
	sd.OutputWindow = m_hWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(m_dxgiFactory->CreateSwapChain(
		m_CommandQueue.Get(),
		&sd,
		m_SwapChain.GetAddressOf()));
}

void CMeshManager::Create_RtvAndDsv_DescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = m_SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}

void CMeshManager::Resize_SwapChainBuffers()
{
	assert(m_d3dDevice);
	assert(m_SwapChain);
	assert(m_DirectCmdListAlloc);

	FlushCommandQueue();

	ThrowIfFailed(m_CommandList->Reset(m_DirectCmdListAlloc.Get(), nullptr));

	for (int i = 0; i < m_SwapChainBufferCount; ++i)
		m_SwapChainBuffer[i].Reset();

	m_DepthStencilBuffer.Reset();

	ThrowIfFailed(m_SwapChain->ResizeBuffers(
		m_SwapChainBufferCount,
		m_ClientWidth, m_ClientHeight,
		m_BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
}

void CMeshManager::FlushCommandQueue()
{
	m_CurrentFence++;

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

	if (m_Fence->GetCompletedValue() < m_CurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void CMeshManager::Create_RenderTarget()
{
	m_CurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT i = 0; i < m_SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));
		m_d3dDevice->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, m_RtvDescriptorSize);
	}

}

void CMeshManager::Create_DepthStencil_Buff_And_View()
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	depthStencilDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = m_DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = m_DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	m_d3dDevice->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

D3D12_CPU_DESCRIPTOR_HANDLE CMeshManager::DepthStencilView()
{
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void CMeshManager::Execute_Init_Commands()
{
	ThrowIfFailed(m_CommandList->Close());
	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();
}

void CMeshManager::Update_ViewPort_And_Scissor()
{
	m_ScreenViewport.TopLeftX = 0;
	m_ScreenViewport.TopLeftY = 0;
	m_ScreenViewport.Width = static_cast<float>(m_ClientWidth);
	m_ScreenViewport.Height = static_cast<float>(m_ClientHeight);
	m_ScreenViewport.MinDepth = 0.0f;
	m_ScreenViewport.MaxDepth = 1.0f;

	m_ScissorRect = { 0, 0, m_ClientWidth, m_ClientHeight };
}



D3D12_CPU_DESCRIPTOR_HANDLE CMeshManager::CurrentBackBufferView()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_CurrBackBuffer,
		m_RtvDescriptorSize);
}

ID3D12Resource* CMeshManager::CurrentBackBuffer()
{
	return m_SwapChainBuffer[m_CurrBackBuffer].Get();
}


void CMeshManager::Init_MeshManager(HWND hWnd, int ClientWidth, int ClientHeight)
{
	m_hWnd = hWnd;

	m_ClientWidth = ClientWidth;
	m_ClientHeight = ClientHeight;

	EnableDebugLayer_CreateFactory();

	Create_Device();

	CreateFence_GetDescriptorsSize();

	Check_Multisample_Quality();

	Create_CommandList_Allocator_Queue();

	Create_SwapChain();

	Create_RtvAndDsv_DescriptorHeaps();

	Resize_SwapChainBuffers();

	Create_RenderTarget();

	Create_DepthStencil_Buff_And_View();

	Create_ScreenAlignedQuad_Shaders_And_InputLayout();

	Create_ScreenAlignedQuad_Geometry();

	Create_RootSignature();
	
	Create_PipelineStateObject();

	auto CrateTex = std::make_unique<Texture>();
	//CrateTex->Name = "WoodCrateTex";
	//CrateTex->Filename = L"./texture256.bmp";

	CrateTex->Resource = CreateTexture(m_d3dDevice.Get(),
		m_CommandList.Get(), NULL, m_ClientWidth * m_ClientHeight * 4, CrateTex->UploadHeap);

	m_SQABuff->Textures = std::move(CrateTex);
		
	Create_ShaderRVHeap_And_View();

	Execute_Init_Commands();

	Update_ViewPort_And_Scissor();

	m_BackBuffer = new unsigned char[m_ClientWidth * m_ClientHeight * 4];
		
	m_Timer.TimerStart(30);

	m_Res = NULL;

	m_VertCount = 24;
	m_TriangleCount = 12;

	Read_BMP_File(".//texture.bmp");

	vector3 VertBuffTemp[24] = {
		vector3(-5.000000,-5.000000,-5.000000),
		vector3(-5.000000,-5.000000,5.000000),
		vector3(5.000000,-5.000000,5.000000),
		vector3(5.000000,-5.000000,-5.000000),
		vector3(-5.000000,5.000000,-5.000000),
		vector3(5.000000,5.000000,-5.000000),
		vector3(5.000000,5.000000,5.000000),
		vector3(-5.000000,5.000000,5.000000),
		vector3(-5.000000,-5.000000,-5.000000),
		vector3(5.000000,-5.000000,-5.000000),
		vector3(5.000000,5.000000,-5.000000),
		vector3(-5.000000,5.000000,-5.000000),
		vector3(5.000000,-5.000000,-5.000000),
		vector3(5.000000,-5.000000,5.000000),
		vector3(5.000000,5.000000,5.000000),
		vector3(5.000000,5.000000,-5.000000),
		vector3(5.000000,-5.000000,5.000000),
		vector3(-5.000000,-5.000000,5.000000),
		vector3(-5.000000,5.000000,5.000000),
		vector3(5.000000,5.000000,5.000000),
		vector3(-5.000000,-5.000000,5.000000),
		vector3(-5.000000,-5.000000,-5.000000),
		vector3(-5.000000,5.000000,-5.000000),
		vector3(-5.000000,5.000000,5.000000) };

	tex_coord2 TexCoordTemp[24] = {
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0,
		0,					m_TextureHeight - 1,
		0,					m_TextureHeight - 1,
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0,
		0,					m_TextureHeight - 1,
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0,
		0,					m_TextureHeight - 1,
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0,
		0,					m_TextureHeight - 1,
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0,
		0,					m_TextureHeight - 1,
		m_TextureWidth - 1,	m_TextureHeight - 1,
		m_TextureWidth - 1,	0,
		0,					0 };

	DWORD IndexBuffTemp[36] = {
		0,2,1,
		2,0,3,
		4,6,5,
		6,4,7,
		8,10,9,
		10,8,11,
		12,14,13,
		14,12,15,
		16,18,17,
		18,16,19,
		20,22,21,
		22,20,23 };

	m_VertBuff = NULL;
	m_VertBuff = new vector3[24];

	m_VertBuffTransformed = NULL;
	m_VertBuffTransformed = new vector3[24];

	m_TexCoord = NULL;
	m_TexCoord = new tex_coord2[24];

	m_IndexBuff = NULL;
	m_IndexBuff = new DWORD[36];

	memcpy(m_VertBuff, VertBuffTemp, 24 * sizeof(vector3));
	memcpy(m_TexCoord, TexCoordTemp, 24 * sizeof(tex_coord2));
	memcpy(m_IndexBuff, IndexBuffTemp, 36 * sizeof(DWORD));
}

void CMeshManager::Update_MeshManager()
{
	m_Timer.CalculateFPS();
	float ElapsedTime = m_Timer.GetElaspedTime();

	static float Angle = 0.0f;

	matrix4x4 MatRotateX = {
		1, 0, 0, 0,
		0, cosf(Angle), sinf(Angle), 0,
		0,-sinf(Angle),  cosf(Angle), 0,
		0, 0, 0, 1 };

	matrix4x4 MatRotateY = {
		cosf(Angle), 0, -sinf(Angle), 0,
		0, 1, 0, 0,
		sinf(Angle), 0, cosf(Angle), 0,
		0, 0, 0, 1 };

	matrix4x4 MatRotateZ = {
		cosf(Angle), sinf(Angle), 0, 0,
		-sinf(Angle), cosf(Angle), 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };

	Angle += ElapsedTime;
	if (Angle > PI2)
		Angle = 0.0f;

	matrix4x4 MatWorld = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 18.0f, 1 };

	matrix4x4 MatView = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };

	float Fov = PI / 2.0f; // FOV 90 degree
	float Aspect = (float)m_ClientWidth / m_ClientHeight;
	float ZFar = 100.0f;
	float ZNear = 0.1f;

	float h, w, Q;

	w = (1.0f / tanf(Fov * 0.5f)) / Aspect;
	h = 1.0f / tanf(Fov * 0.5f);
	Q = ZFar / (ZFar - ZNear);

	/*
	//������ ������ ������� �������� ��� �� �����
	matrix4x4 MatProj={
		w, 0, 0, 0,
		0, h, 0, 0,
		0, 0, Q, 1,
		0, 0, -Q*fZNear, 0 };
	*/

	matrix4x4 MatProj = {
		w, 0, 0, 0,
		0, h, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1 };

	float Alpha = 0.5f * m_ClientWidth;
	float Beta = 0.5f * m_ClientHeight;

	matrix4x4 MatScreen = {
		Alpha,  0,	    0,    0,
		0,      -Beta,  0,    0,
		0,		0,		1,    0,
		Alpha,  Beta,	0,    1 };

	for (UINT i = 0; i < m_VertCount; i++)
	{
		vector3 VecTemp;

		VecTemp = Vec3_Mat4x4_Mul(m_VertBuff[i], MatRotateX);
		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatRotateY);
		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatRotateZ);
		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatWorld);
		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatView);
		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatProj);

		VecTemp.x = VecTemp.x / VecTemp.z;
		VecTemp.y = VecTemp.y / VecTemp.z;

		VecTemp = Vec3_Mat4x4_Mul(VecTemp, MatScreen);

		m_VertBuffTransformed[i] = VecTemp;
	}
}

void CMeshManager::Draw_MeshManager()
{
	ThrowIfFailed(m_DirectCmdListAlloc->Reset());

	ThrowIfFailed(m_CommandList->Reset(m_DirectCmdListAlloc.Get(), nullptr));

	m_CommandList->RSSetViewports(1, &m_ScreenViewport);
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	m_CommandList->SetPipelineState(m_PSOSAQ.Get());

	float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f };
	m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), ClearColor, 0, nullptr);
	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	Draw_MeshManager2();

	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeapsSAQ[] = { m_SrvDescriptorHeapSAQ.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeapsSAQ), descriptorHeapsSAQ);

	m_CommandList->SetGraphicsRootDescriptorTable(0, m_SrvDescriptorHeapSAQ->GetGPUDescriptorHandleForHeapStart());

	m_CommandList->IASetVertexBuffers(0, 1, &m_SQABuff->VertexBufferView());
	m_CommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	//����� 4 ������� � ������ � 2 ������������
	m_CommandList->DrawInstanced(4, 2, 0, 0);


	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_CommandList->Close());

	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(m_SwapChain->Present(0, 0));

	m_CurrBackBuffer = (m_CurrBackBuffer + 1) % m_SwapChainBufferCount;

	FlushCommandQueue();
}

void CMeshManager::Read_BMP_File(const char* szTexFileName)
{
	FILE* Fp;

	fopen_s(&Fp, szTexFileName, "rb");
	if (Fp == NULL) printf("Error Open File");

	BITMAPFILEHEADER Bfh;
	fread(&Bfh, sizeof(Bfh), 1, Fp);

	BITMAPINFOHEADER Bih;
	fread(&Bih, sizeof(Bih), 1, Fp);

	fseek(Fp, Bfh.bfOffBits, SEEK_SET);

	m_Res = new unsigned char[Bih.biWidth * Bih.biHeight * 3];
	fread(m_Res, Bih.biWidth * Bih.biHeight * 3, 1, Fp);

	fclose(Fp);

	m_TextureWidth = Bih.biWidth;
	m_TextureHeight = Bih.biHeight;
}

vector3 CMeshManager::Vec3_Mat4x4_Mul(vector3 &VecIn, matrix4x4 MatIn)
{
	vector3 VecOut;

	for (int j = 0; j < 3; j++)
	{
		float Sum = 0.0f;
		int i;
		for (i = 0; i < 3; i++)
		{
			Sum += VecIn.Vec[i] * MatIn[i][j];
		}

		Sum += MatIn[i][j];
		VecOut.Vec[j] = Sum;
	}

	return VecOut;
}

void CMeshManager::Draw_Textured_Triangle(vector3 VecIn1, tex_coord2 TexIn1,
	vector3 VecIn2, tex_coord2 TexIn2,
	vector3 VecIn3, tex_coord2 TexIn3)
{
	int Side;
	float x1, x2, x3;
	float y1, y2, y3;
	float iz1, uiz1, viz1, iz2, uiz2, viz2, iz3, uiz3, viz3;
	float Tempf;

	x1 = VecIn1.x;
	y1 = VecIn1.y;
	x2 = VecIn2.x;
	y2 = VecIn2.y;
	x3 = VecIn3.x;
	y3 = VecIn3.y;

	iz1 = 1.0f / VecIn1.z;
	iz2 = 1.0f / VecIn2.z;
	iz3 = 1.0f / VecIn3.z;

	uiz1 = TexIn1.tu * iz1;
	viz1 = TexIn1.tv * iz1;
	uiz2 = TexIn2.tu * iz2;
	viz2 = TexIn2.tv * iz2;
	uiz3 = TexIn3.tu * iz3;
	viz3 = TexIn3.tv * iz3;

#define swapfloat(x, y) Tempf = x; x = y; y = Tempf;

	if (y1 > y2)
	{
		swapfloat(x1, x2);
		swapfloat(y1, y2);
		swapfloat(iz1, iz2);
		swapfloat(uiz1, uiz2);
		swapfloat(viz1, viz2);
	}
	if (y1 > y3)
	{
		swapfloat(x1, x3);
		swapfloat(y1, y3);
		swapfloat(iz1, iz3);
		swapfloat(uiz1, uiz3);
		swapfloat(viz1, viz3);
	}
	if (y2 > y3)
	{
		swapfloat(x2, x3);
		swapfloat(y2, y3);
		swapfloat(iz2, iz3);
		swapfloat(uiz2, uiz3);
		swapfloat(viz2, viz3);
	}

#undef swapfloat

	if (y2 > y1 && y3 > y2)
	{
		float dxdy1 = (x2 - x1) / (y2 - y1);
		float dxdy2 = (x3 - x1) / (y3 - y1);
		Side = dxdy2 > dxdy1;
	}

	if (y1 == y2)
		Side = x1 > x2;
	if (y2 == y3)
		Side = x3 > x2;

	if (!Side)
	{
		m_xl = x1;
		m_ul = uiz1;
		m_vl = viz1;
		m_zl = iz1;

		m_dxdyl = (x3 - x1) / (y3 - y1);
		m_dudyl = (uiz3 - uiz1) / (y3 - y1);
		m_dvdyl = (viz3 - viz1) / (y3 - y1);
		m_dzdyl = (iz3 - iz1) / (y3 - y1);

		if (y1 < y2)
		{
			m_xr = x1;
			m_ur = uiz1;
			m_vr = viz1;
			m_zr = iz1;

			m_dxdyr = (x2 - x1) / (y2 - y1);
			m_dudyr = (uiz2 - uiz1) / (y2 - y1);
			m_dvdyr = (viz2 - viz1) / (y2 - y1);
			m_dzdyr = (iz2 - iz1) / (y2 - y1);

			Draw_Textured_Poly((int)y1, (int)y2);
		}
		if (y2 < y3)
		{
			m_xr = x2;
			m_ur = uiz2;
			m_vr = viz2;
			m_zr = iz2;

			m_dxdyr = (x3 - x2) / (y3 - y2);
			m_dudyr = (uiz3 - uiz2) / (y3 - y2);
			m_dvdyr = (viz3 - viz2) / (y3 - y2);
			m_dzdyr = (iz3 - iz2) / (y3 - y2);

			Draw_Textured_Poly((int)y2, (int)y3);
		}

	}
	else
	{
		m_xr = x1;
		m_ur = uiz1;
		m_vr = viz1;
		m_zr = iz1;

		m_dxdyr = (x3 - x1) / (y3 - y1);
		m_dudyr = (uiz3 - uiz1) / (y3 - y1);
		m_dvdyr = (viz3 - viz1) / (y3 - y1);
		m_dzdyr = (iz3 - iz1) / (y3 - y1);

		if (y1 < y2)
		{
			m_xl = x1;
			m_ul = uiz1;
			m_vl = viz1;
			m_zl = iz1;

			m_dxdyl = (x2 - x1) / (y2 - y1);
			m_dudyl = (uiz2 - uiz1) / (y2 - y1);
			m_dvdyl = (viz2 - viz1) / (y2 - y1);
			m_dzdyl = (iz2 - iz1) / (y2 - y1);

			Draw_Textured_Poly((int)y1, (int)y2);
		}
		if (y2 < y3)
		{
			m_xl = x2;
			m_ul = uiz2;
			m_vl = viz2;
			m_zl = iz2;

			m_dxdyl = (x3 - x2) / (y3 - y2);
			m_dudyl = (uiz3 - uiz2) / (y3 - y2);
			m_dvdyl = (viz3 - viz2) / (y3 - y2);
			m_dzdyl = (iz3 - iz2) / (y3 - y2);

			Draw_Textured_Poly((int)y2, (int)y3);
		}
	}
}

void CMeshManager::Draw_Textured_Poly(int y1, int y2)
{
	float ui, vi, zi;
	float du, dv, dz;

	for (int y = y1; y < y2; y++)
	{
		if ((m_xr - m_xl) > 0)
		{
			du = (m_ur - m_ul) / (m_xr - m_xl);
			dv = (m_vr - m_vl) / (m_xr - m_xl);
			dz = (m_zr - m_zl) / (m_xr - m_xl);
		}
		else
		{
			du = 0;
			dv = 0;
			dz = 0;
		}

		int xln = (int)m_xl;

		float dxt = 1 - (m_xl - xln);

		zi = m_zl + dxt * dz;
		ui = m_ul + dxt * du;
		vi = m_vl + dxt * dv;

		for (int x = (int)m_xl; x < (int)m_xr; x++)
		{
			float z = 1.0f / zi;
			float u = ui * z;
			float v = vi * z;

			int t = (int)u + (((int)v) * m_TextureWidth);

			if (t < 0 || t >(m_TextureWidth * m_TextureHeight - 1))
				continue;

			t = t * 3;

			int Index = y * 4 * m_ClientWidth + x * 4;

			m_BackBuffer[Index + 0] = (BYTE)m_Res[t + 2]; // red
			m_BackBuffer[Index + 1] = (BYTE)m_Res[t + 1]; // green
			m_BackBuffer[Index + 2] = (BYTE)m_Res[t + 0]; // blue
			m_BackBuffer[Index + 3] = 0;

			ui += du;
			vi += dv;
			zi += dz;
		}

		m_xl += m_dxdyl;
		m_ul += m_dudyl;
		m_vl += m_dvdyl;
		m_zl += m_dzdyl;

		m_xr += m_dxdyr;
		m_ur += m_dudyr;
		m_vr += m_dvdyr;
		m_zr += m_dzdyr;
	}
}

void CMeshManager::Create_ScreenAlignedQuad_Shaders_And_InputLayout()
{
	m_VsByteCodeSAQ = d3dUtil::CompileShader(L"Shaders\\saq.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCodeSAQ = d3dUtil::CompileShader(L"Shaders\\saq.hlsl", nullptr, "PS", "ps_5_0");

	m_InputLayoutSAQ =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void CMeshManager::Create_ScreenAlignedQuad_Geometry()
{
	std::array<VertexSAQ, 4> VerticesSAQ =
	{
		VertexSAQ({ DirectX::XMFLOAT3(1.0f,   1.0f, 0.5f) }),
		VertexSAQ({ DirectX::XMFLOAT3(1.0f, -1.0f, 0.5f) }),
		VertexSAQ({ DirectX::XMFLOAT3(-1.0f, 1.0f, 0.5) }),
		VertexSAQ({ DirectX::XMFLOAT3(-1.0f,  -1.0f, 0.5f) })
	};

	const UINT vbSAQByteSize = (UINT)VerticesSAQ.size() * sizeof(VertexSAQ);

	m_SQABuff = std::make_unique<MeshGeometry>();
	m_SQABuff->Name = "SAQ";

	m_SQABuff->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_d3dDevice.Get(),
		m_CommandList.Get(), VerticesSAQ.data(), vbSAQByteSize, m_SQABuff->VertexBufferUploader);

	m_SQABuff->VertexByteStride = sizeof(VertexSAQ);
	m_SQABuff->VertexBufferByteSize = vbSAQByteSize;
}

void CMeshManager::Create_PipelineStateObject()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc_SAQ;
	ZeroMemory(&psoDesc_SAQ, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc_SAQ.InputLayout = { m_InputLayoutSAQ.data(), (UINT)m_InputLayoutSAQ.size() };
	psoDesc_SAQ.pRootSignature = m_RootSignature.Get();
	psoDesc_SAQ.VS =
	{
		reinterpret_cast<BYTE*>(m_VsByteCodeSAQ->GetBufferPointer()),
		m_VsByteCodeSAQ->GetBufferSize()
	};
	psoDesc_SAQ.PS =
	{
		reinterpret_cast<BYTE*>(m_PsByteCodeSAQ->GetBufferPointer()),
		m_PsByteCodeSAQ->GetBufferSize()
	};
	psoDesc_SAQ.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc_SAQ.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc_SAQ.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc_SAQ.SampleMask = UINT_MAX;
	psoDesc_SAQ.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc_SAQ.NumRenderTargets = 1;
	psoDesc_SAQ.RTVFormats[0] = m_BackBufferFormat;
	psoDesc_SAQ.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	psoDesc_SAQ.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	psoDesc_SAQ.DSVFormat = m_DepthStencilFormat;
	ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc_SAQ, IID_PPV_ARGS(&m_PSOSAQ)));
}

void CMeshManager::Create_RootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> SerializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> ErrorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		SerializedRootSig.GetAddressOf(), ErrorBlob.GetAddressOf());

	if (ErrorBlob != nullptr)
	{
		::OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(m_d3dDevice->CreateRootSignature(
		0,
		SerializedRootSig->GetBufferPointer(),
		SerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&m_RootSignature)));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CMeshManager::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC PointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC PointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC LinearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC LinearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC AnisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC AnisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		PointWrap, PointClamp,
		LinearWrap, LinearClamp,
		AnisotropicWrap, AnisotropicClamp };
}

void CMeshManager::Create_ShaderRVHeap_And_View()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeapSAQ)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor1(m_SrvDescriptorHeapSAQ->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = m_SQABuff->Textures.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = m_SQABuff->Textures.get()->Resource->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	m_d3dDevice->CreateShaderResourceView(m_SQABuff->Textures.get()->Resource.Get(), &srvDesc, hDescriptor1);
}

Microsoft::WRL::ComPtr<ID3D12Resource> CMeshManager::CreateTexture(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& UploadBuffer)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Texture;

	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = m_ClientWidth;
	textureDesc.Height = m_ClientHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf())));

	const UINT64 UploadBufferSize = GetRequiredIntermediateSize(m_Texture.Get(), 0, 1);

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(UploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&UploadBuffer)));

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_Texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	return m_Texture;
}

void CMeshManager::Draw_MeshManager2()
{
	//������� pBuffer (�����)
	for (int x = 0; x < m_ClientWidth; x++)
	{
		for (int y = 0; y < m_ClientHeight; y++)
		{
			int Index = y * m_ClientWidth * 4 + x * 4;

			m_BackBuffer[Index + 0] = 0; //red
			m_BackBuffer[Index + 1] = 32; //green
			m_BackBuffer[Index + 2] = 77; //blue
			m_BackBuffer[Index + 3] = 0; //Alpha
		}
	}

	for (UINT i = 0; i < m_TriangleCount; i++)
	{
		vector3 Vec1 = m_VertBuffTransformed[m_IndexBuff[i * 3 + 0]];
		vector3 Vec2 = m_VertBuffTransformed[m_IndexBuff[i * 3 + 1]];
		vector3 Vec3 = m_VertBuffTransformed[m_IndexBuff[i * 3 + 2]];

		tex_coord2 TexCoord1 = m_TexCoord[m_IndexBuff[i * 3 + 0]];
		tex_coord2 TexCoord2 = m_TexCoord[m_IndexBuff[i * 3 + 1]];
		tex_coord2 TexCoord3 = m_TexCoord[m_IndexBuff[i * 3 + 2]];

		//���������� ��������������� (�����) ������������ ��������
		//��� ������������ ������ ������������
		//������� � �������� �����������
		float s = (Vec2.x - Vec1.x) * (Vec3.y - Vec1.y) - (Vec2.y - Vec1.y) * (Vec3.x - Vec1.x);

		if (s <= 0)
			continue;

		//������ ������� �����������
		Draw_Textured_Triangle(Vec1, TexCoord1, Vec2, TexCoord2, Vec3, TexCoord3);
	}

	//�������� m_BackBuffer � backbuffer DX12
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_SQABuff->Textures.get()->Resource.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = m_BackBuffer;
	textureData.RowPitch = m_ClientWidth * 4;
	textureData.SlicePitch = textureData.RowPitch * m_ClientHeight;

	UpdateSubresources(m_CommandList.Get(), m_SQABuff->Textures.get()->Resource.Get(), m_SQABuff->Textures.get()->UploadHeap.Get(), 0, 0, 1, &textureData);
	
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_SQABuff->Textures.get()->Resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}