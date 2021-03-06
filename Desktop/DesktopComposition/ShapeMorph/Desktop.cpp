#include "pch.h"
#include "SquareCircleMorph.h"


extern "C" IMAGE_DOS_HEADER __ImageBase;

extern "C"
{
	HRESULT __stdcall OS_RoGetActivationFactory(HSTRING classId, winrt::guid & iid, void** factory) noexcept;
}

#ifdef _M_IX86
#pragma comment(linker, "/alternatename:_OS_RoGetActivationFactory@12=_RoGetActivationFactory@12")
#else
#pragma comment(linker, "/alternatename:OS_RoGetActivationFactory=RoGetActivationFactory")
#endif

bool starts_with(std::wstring_view value, std::wstring_view match) noexcept
{
	return 0 == value.compare(0, match.size(), match);
}

int32_t WINRT_CALL WINRT_RoGetActivationFactory(void* classId, winrt::guid const& iid, void** factory) noexcept
//int32_t WINRT_CALL WINRT_RoGetActivationFactory(HSTRING classId, winrt::guid iid, void** factory) noexcept
{
	*factory = nullptr;
	std::wstring_view name{ WindowsGetStringRawBuffer((HSTRING)classId, nullptr), WindowsGetStringLen((HSTRING)classId) };
	HMODULE library{ nullptr };

	if (starts_with(name, L"Microsoft.Graphics."))
	{
		library = LoadLibraryW(L"Microsoft.Graphics.Canvas.dll");
	}
	else
	{
		return OS_RoGetActivationFactory((HSTRING)classId, (winrt::guid &)iid, factory);
	}

	if (!library)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	using DllGetActivationFactory = HRESULT __stdcall(HSTRING classId, void** factory);
	auto call = reinterpret_cast<DllGetActivationFactory*>(GetProcAddress(library, "DllGetActivationFactory"));

	if (!call)
	{
		HRESULT const hr = HRESULT_FROM_WIN32(GetLastError());
		WINRT_VERIFY(FreeLibrary(library));
		return hr;
	}

	winrt::com_ptr<winrt::Windows::Foundation::IActivationFactory> activation_factory;
	HRESULT const hr = call((HSTRING)classId, activation_factory.put_void());

	if (FAILED(hr))
	{
		WINRT_VERIFY(FreeLibrary(library));
		return hr;
	}

	if (iid != winrt::guid_of<winrt::Windows::Foundation::IActivationFactory>())
	{
		return activation_factory->QueryInterface(iid, factory);
	}

	*factory = activation_factory.detach();
	return S_OK;
}

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Composition::Desktop;
using namespace Microsoft::Graphics::Canvas;
using namespace Microsoft::Graphics::Canvas::Geometry;
using namespace Windows::Foundation::Numerics;

auto CreateDispatcherQueueController()
{
	namespace abi = ABI::Windows::System;

	DispatcherQueueOptions options
	{
		sizeof(DispatcherQueueOptions),
		DQTYPE_THREAD_CURRENT,
		DQTAT_COM_STA
	};

	Windows::System::DispatcherQueueController controller{ nullptr };
	check_hresult(CreateDispatcherQueueController(options, reinterpret_cast<abi::IDispatcherQueueController**>(put_abi(controller))));
	return controller;
}

DesktopWindowTarget CreateDesktopWindowTarget(Compositor const& compositor, HWND window)
{
	namespace abi = ABI::Windows::UI::Composition::Desktop;

	auto interop = compositor.as<abi::ICompositorDesktopInterop>();
	DesktopWindowTarget target{ nullptr };
	check_hresult(interop->CreateDesktopWindowTarget(window, true, reinterpret_cast<abi::IDesktopWindowTarget**>(put_abi(target))));
	return target;
}

CanvasGeometry BuildSquareGeometry()
{
	// use Win2D's CanvasPathBuilder to create a simple path

	auto device = CanvasDevice::GetSharedDevice();

	auto builder = CanvasPathBuilder(device);

	builder.SetFilledRegionDetermination(CanvasFilledRegionDetermination::Winding);
	builder.BeginFigure(float2(-90, -146));
	builder.AddCubicBezier(float2(-90, -146), float2(176, -148.555F), float2(176, -148.555F));
	builder.AddCubicBezier(float2(176, -148.555F), float2(174.445F, 121.445F), float2(174.445F, 121.445F));
	builder.AddCubicBezier(float2(174.445F, 121.445F), float2(-91.555F, 120), float2(-91.555F, 120));
	builder.AddCubicBezier(float2(-91.555F, 120), float2(-90, -146), float2(-90, -146));
	builder.EndFigure(CanvasFigureLoop::Closed);
	return CanvasGeometry::CreatePath(builder);
}

CanvasGeometry BuildCircleGeometry()
{
	// use Win2D's CanvasPathBuilder to create a simple path
	auto builder = CanvasPathBuilder(CanvasDevice::GetSharedDevice());

	builder.SetFilledRegionDetermination(CanvasFilledRegionDetermination::Winding);
	builder.BeginFigure(float2(42.223F, -146));
	builder.AddCubicBezier(float2(115.248F, -146), float2(174.445F, -86.13F), float2(174.445F, -12.277F));
	builder.AddCubicBezier(float2(174.445F, 61.576F), float2(115.248F, 121.445F), float2(42.223F, 121.445F));
	builder.AddCubicBezier(float2(-30.802F, 121.445F), float2(-90, 61.576F), float2(-90, -12.277F));
	builder.AddCubicBezier(float2(-90, -86.13F), float2(-30.802F, -146), float2(42.223F, -146));
	builder.EndFigure(CanvasFigureLoop::Closed);
	return CanvasGeometry::CreatePath(builder);
}

Windows::UI::Composition::CompositionLinearGradientBrush CreateGradientBrush(Compositor const& compositor)
{
	auto gradBrush = compositor.CreateLinearGradientBrush();
	gradBrush.ColorStops().InsertAt(0, compositor.CreateColorGradientStop(0.0f, Windows::UI::Colors::Orange()));
	gradBrush.ColorStops().InsertAt(1, compositor.CreateColorGradientStop(0.5f, Windows::UI::Colors::Yellow()));
	gradBrush.ColorStops().InsertAt(2, compositor.CreateColorGradientStop(1.0f, Windows::UI::Colors::Red()));
	return gradBrush;
}

void PathMorphImperative(Compositor const& compositor, VisualCollection const& visuals, float x, float y) {
	// Same steps as for SimpleShapeImperative_Click to create, size and host a ShapeVisual
	ShapeVisual shape = compositor.CreateShapeVisual();
	//ElementCompositionPreview::SetElementChildVisual(VectorHost(), shape);
	//shape.Size(float2((float)VectorHost().Width(), (float)VectorHost().Height()));
	shape.Size(float2(640, 480));
	shape.Offset(float3(x, y, 1.0f));

	// Call helper functions that use Win2D to build square and circle path geometries and create CompositionPath's for them
	CanvasGeometry square = BuildSquareGeometry();
	CompositionPath squarePath = CompositionPath(square);

	CanvasGeometry circle = BuildCircleGeometry();
	CompositionPath circlePath = CompositionPath(circle);

	// Create a CompositionPathGeometry, CompositionSpriteShape and set offset and fill
	CompositionPathGeometry compositionPathGeometry = compositor.CreatePathGeometry(squarePath);
	CompositionSpriteShape spriteShape = compositor.CreateSpriteShape(compositionPathGeometry);
	spriteShape.Offset(float2(150, 200));
	spriteShape.FillBrush(CreateGradientBrush(compositor));

	// Create a PathKeyFrameAnimation to set up the path morph passing in the circle and square paths
	auto playAnimation = compositor.CreatePathKeyFrameAnimation();
	playAnimation.Duration(std::chrono::seconds(4));
	playAnimation.InsertKeyFrame(0, squarePath);
	playAnimation.InsertKeyFrame(0.3F, circlePath);
	playAnimation.InsertKeyFrame(0.6F, circlePath);
	playAnimation.InsertKeyFrame(1.0F, squarePath);

	// Make animation repeat forever and start it
	playAnimation.IterationBehavior(AnimationIterationBehavior::Forever);
	playAnimation.Direction(AnimationDirection::Alternate);
	compositionPathGeometry.StartAnimation(L"Path", playAnimation);

	// Add the SpriteShape to our shape visual
	shape.Shapes().Append(spriteShape);
	visuals.InsertAtTop(shape);
}

template <typename T>
struct DesktopWindow
{
	static T* GetThisFromHandle(HWND const window) noexcept
	{
		return reinterpret_cast<T *>(GetWindowLongPtr(window, GWLP_USERDATA));
	}

	static LRESULT __stdcall WndProc(HWND const window, UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
	{
		WINRT_ASSERT(window);

		if (WM_NCCREATE == message)
		{
			auto cs = reinterpret_cast<CREATESTRUCT *>(lparam);
			T* that = static_cast<T*>(cs->lpCreateParams);
			WINRT_ASSERT(that);
			WINRT_ASSERT(!that->m_window);
			that->m_window = window;
			SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));
		}
		else if (T* that = GetThisFromHandle(window))
		{
			return that->MessageHandler(message, wparam, lparam);
		}

		return DefWindowProc(window, message, wparam, lparam);
	}

	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
	{
		if (WM_DESTROY == message)
		{
			PostQuitMessage(0);
			return 0;
		}

		return DefWindowProc(m_window, message, wparam, lparam);
	}

protected:

	using base_type = DesktopWindow<T>;
	HWND m_window = nullptr;
};

struct Window : DesktopWindow<Window>
{
	Window() noexcept
	{
		WNDCLASS wc{};
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
		wc.lpszClassName = L"Windows::UI::Composition in Win32 Sample";
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		RegisterClass(&wc);
		WINRT_ASSERT(!m_window);

		WINRT_VERIFY(CreateWindow(wc.lpszClassName,
			L"Windows::UI::Composition in Win32 Sample",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			nullptr, nullptr, wc.hInstance, this));

		WINRT_ASSERT(m_window);
	}

	LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
	{
		// TODO: handle messages here...

		return base_type::MessageHandler(message, wparam, lparam);
	}

	void PrepareVisuals()
	{
		Compositor compositor;
		m_target = CreateDesktopWindowTarget(compositor, m_window);
		auto root = compositor.CreateSpriteVisual();
		root.RelativeSizeAdjustment({ 1.0f, 1.0f });
		root.Brush(compositor.CreateColorBrush({ 0xFF, 0xEF, 0xE4 , 0xB0 }));
		m_target.Root(root);
		auto visuals = root.Children();

		/*AddVisual(visuals, 100.0f, 100.0f);
		AddVisual(visuals, 220.0f, 100.0f);
		AddVisual(visuals, 100.0f, 220.0f);
		AddVisual(visuals, 220.0f, 220.0f);*/
		//PathMorphImperative(compositor, visuals, 20.0f, 20.0f);
		m_shapeTool = std::make_unique< AnimatedVisuals::SquareCircleMorph>();
		m_shapeTool->TryCreateAnimatedVisual(compositor, visuals);
		m_shapeTool->Play();
	}

	void AddVisual(VisualCollection const& visuals, float x, float y)
	{
		auto compositor = visuals.Compositor();
		auto visual = compositor.CreateSpriteVisual();

		static Color colors[] =
		{
			{ 0xDC, 0x5B, 0x9B, 0xD5 },
		{ 0xDC, 0xFF, 0xC0, 0x00 },
		{ 0xDC, 0xED, 0x7D, 0x31 },
		{ 0xDC, 0x70, 0xAD, 0x47 },
		};

		static unsigned last = 0;
		unsigned const next = ++last % _countof(colors);
		visual.Brush(compositor.CreateColorBrush(colors[next]));
		visual.Size({ 100.0f, 100.0f });
		visual.Offset({ x, y, 0.0f, });

		visuals.InsertAtTop(visual);
	}

private:

	DesktopWindowTarget m_target{ nullptr };
	std::unique_ptr<AnimatedVisuals::SquareCircleMorph> m_shapeTool{ nullptr };
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	init_apartment(apartment_type::single_threaded);
	auto controller = CreateDispatcherQueueController();

	Window window;
	window.PrepareVisuals();
	//BuildSquareGeometry();
	MSG message;

	while (GetMessage(&message, nullptr, 0, 0))
	{
		DispatchMessage(&message);
	}
}
