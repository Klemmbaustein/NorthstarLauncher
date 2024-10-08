#include "debugoverlay.h"

#include "dedicated/dedicated.h"
#include "core/convar/cvar.h"
#include "core/math/vector.h"
#include "server/ai_helper.h"

AUTOHOOK_INIT()

enum OverlayType_t
{
	OVERLAY_BOX = 0,
	OVERLAY_SPHERE,
	OVERLAY_LINE,
	OVERLAY_SMARTAMMO,
	OVERLAY_TRIANGLE,
	OVERLAY_SWEPT_BOX,
	// [Fifty]: the 2 bellow i did not confirm, rest are good
	OVERLAY_BOX2,
	OVERLAY_CAPSULE
};

struct OverlayBase_t
{
	OverlayBase_t()
	{
		m_Type = OVERLAY_BOX;
		m_nServerCount = -1;
		m_nCreationTick = -1;
		m_flEndTime = 0.0f;
		m_pNextOverlay = NULL;
	}

	OverlayType_t m_Type; // What type of overlay is it?
	int m_nCreationTick; // Duration -1 means go away after this frame #
	int m_nServerCount; // Latch server count, too
	float m_flEndTime; // When does this box go away
	OverlayBase_t* m_pNextOverlay;
	__int64 m_pUnk;
};

struct OverlayLine_t : public OverlayBase_t
{
	OverlayLine_t() { m_Type = OVERLAY_LINE; }

	Vector3 origin;
	Vector3 dest;
	int r;
	int g;
	int b;
	int a;
	bool noDepthTest;
};

struct OverlayBox_t : public OverlayBase_t
{
	OverlayBox_t() { m_Type = OVERLAY_BOX; }

	Vector3 origin;
	Vector3 mins;
	Vector3 maxs;
	QAngle angles;
	int r;
	int g;
	int b;
	int a;
};

struct OverlayTriangle_t : public OverlayBase_t
{
	OverlayTriangle_t() { m_Type = OVERLAY_TRIANGLE; }

	Vector3 p1;
	Vector3 p2;
	Vector3 p3;
	int r;
	int g;
	int b;
	int a;
	bool noDepthTest;
};

struct OverlaySweptBox_t : public OverlayBase_t
{
	OverlaySweptBox_t() { m_Type = OVERLAY_SWEPT_BOX; }

	Vector3 start;
	Vector3 end;
	Vector3 mins;
	Vector3 maxs;
	QAngle angles;
	int r;
	int g;
	int b;
	int a;
};

struct OverlaySphere_t : public OverlayBase_t
{
	OverlaySphere_t() { m_Type = OVERLAY_SPHERE; }

	Vector3 vOrigin;
	float flRadius;
	int nTheta;
	int nPhi;
	int r;
	int g;
	int b;
	int a;
	bool m_bWireframe;
};

static bool (*OverlayBase_t__IsDead)(OverlayBase_t* a1);
static void (*OverlayBase_t__DestroyOverlay)(OverlayBase_t* a1);

static ConVar* Cvar_enable_debug_overlays;

LPCRITICAL_SECTION s_OverlayMutex;

OverlayBase_t** s_pOverlays;

int* g_nRenderTickCount;
int* g_nOverlayTickCount;

// clang-format off
AUTOHOOK(DrawOverlay, engine.dll + 0xABCB0, 
void, __fastcall, (OverlayBase_t * pOverlay))
// clang-format on
{
	EnterCriticalSection(s_OverlayMutex);

	switch (pOverlay->m_Type)
	{
	case OVERLAY_SMARTAMMO:
	case OVERLAY_LINE:
	{
		OverlayLine_t* pLine = static_cast<OverlayLine_t*>(pOverlay);
		RenderLine(pLine->origin, pLine->dest, Color(pLine->r, pLine->g, pLine->b, pLine->a), pLine->noDepthTest);
	}
	break;
	case OVERLAY_BOX:
	{
		OverlayBox_t* pCurrBox = static_cast<OverlayBox_t*>(pOverlay);
		if (pCurrBox->a > 0)
		{
			RenderBox(
				pCurrBox->origin,
				pCurrBox->angles,
				pCurrBox->mins,
				pCurrBox->maxs,
				Color(pCurrBox->r, pCurrBox->g, pCurrBox->b, pCurrBox->a),
				false,
				false);
		}
		if (pCurrBox->a < 255)
		{
			RenderWireframeBox(
				pCurrBox->origin,
				pCurrBox->angles,
				pCurrBox->mins,
				pCurrBox->maxs,
				Color(pCurrBox->r, pCurrBox->g, pCurrBox->b, 255),
				false,
				false);
		}
	}
	break;
	case OVERLAY_TRIANGLE:
	{
		OverlayTriangle_t* pTriangle = static_cast<OverlayTriangle_t*>(pOverlay);
		RenderTriangle(
			pTriangle->p1,
			pTriangle->p2,
			pTriangle->p3,
			Color(pTriangle->r, pTriangle->g, pTriangle->b, pTriangle->a),
			pTriangle->noDepthTest);
	}
	break;
	case OVERLAY_SWEPT_BOX:
	{
		OverlaySweptBox_t* pBox = static_cast<OverlaySweptBox_t*>(pOverlay);
		RenderWireframeSweptBox(
			pBox->start, pBox->end, pBox->angles, pBox->mins, pBox->maxs, Color(pBox->r, pBox->g, pBox->b, pBox->a), false);
	}
	break;
	case OVERLAY_SPHERE:
	{
		OverlaySphere_t* pSphere = static_cast<OverlaySphere_t*>(pOverlay);
		RenderSphere(
			pSphere->vOrigin,
			pSphere->flRadius,
			pSphere->nTheta,
			pSphere->nPhi,
			Color(pSphere->r, pSphere->g, pSphere->b, pSphere->a),
			false);
	}
	break;
	default:
	{
		spdlog::warn("Unimplemented overlay type {}", pOverlay->m_Type);
	}
	break;
	}

	LeaveCriticalSection(s_OverlayMutex);
}

// clang-format off
AUTOHOOK(DrawAllOverlays, engine.dll + 0xAB780, 
void, __fastcall, (bool bRender))
// clang-format on
{
	EnterCriticalSection(s_OverlayMutex);

	OverlayBase_t* pCurrOverlay = *s_pOverlays; // rbx
	OverlayBase_t* pPrevOverlay = nullptr; // rsi
	OverlayBase_t* pNextOverlay = nullptr; // rdi

	int m_nCreationTick; // eax
	bool bShouldDraw; // zf
	int m_pUnk; // eax

	while (pCurrOverlay)
	{
		if (OverlayBase_t__IsDead(pCurrOverlay))
		{
			if (pPrevOverlay)
			{
				pPrevOverlay->m_pNextOverlay = pCurrOverlay->m_pNextOverlay;
			}
			else
			{
				*s_pOverlays = pCurrOverlay->m_pNextOverlay;
			}

			pNextOverlay = pCurrOverlay->m_pNextOverlay;
			OverlayBase_t__DestroyOverlay(pCurrOverlay);
			pCurrOverlay = pNextOverlay;
		}
		else
		{
			if (pCurrOverlay->m_nCreationTick == -1)
			{
				m_pUnk = pCurrOverlay->m_pUnk;

				if (m_pUnk == -1)
				{
					bShouldDraw = true;
				}
				else
				{
					bShouldDraw = m_pUnk == *g_nOverlayTickCount;
				}
			}
			else
			{
				bShouldDraw = pCurrOverlay->m_nCreationTick == *g_nRenderTickCount;
			}

			if (bShouldDraw && bRender && (Cvar_enable_debug_overlays->GetBool() || pCurrOverlay->m_Type == OVERLAY_SMARTAMMO))
			{
				// call the new function, not the original
				// note: if there is a beter way to call the hooked version of an
				// autohook func then that would be better than this
				__autohookfuncDrawOverlay(pCurrOverlay);
			}

			pPrevOverlay = pCurrOverlay;
			pCurrOverlay = pCurrOverlay->m_pNextOverlay;
		}
	}

	if (bRender && Cvar_enable_debug_overlays->GetBool())
	{
		g_pAIHelper->DrawNavmeshPolys();
	}

	LeaveCriticalSection(s_OverlayMutex);
}

ON_DLL_LOAD_CLIENT_RELIESON("engine.dll", DebugOverlay, ConVar, (CModule module))
{
	AUTOHOOK_DISPATCH()

	OverlayBase_t__IsDead = module.Offset(0xACAC0).RCast<decltype(OverlayBase_t__IsDead)>();
	OverlayBase_t__DestroyOverlay = module.Offset(0xAB680).RCast<decltype(OverlayBase_t__DestroyOverlay)>();

	RenderLine = module.Offset(0x192A70).RCast<decltype(RenderLine)>();
	RenderBox = module.Offset(0x192520).RCast<decltype(RenderBox)>();
	RenderWireframeBox = module.Offset(0x193DA0).RCast<decltype(RenderWireframeBox)>();
	RenderWireframeSweptBox = module.Offset(0x1945A0).RCast<decltype(RenderWireframeSweptBox)>();
	RenderTriangle = module.Offset(0x193940).RCast<decltype(RenderTriangle)>();
	RenderAxis = module.Offset(0x1924D0).RCast<decltype(RenderAxis)>();
	RenderSphere = module.Offset(0x194170).RCast<decltype(RenderSphere)>();
	RenderUnknown = module.Offset(0x1924E0).RCast<decltype(RenderUnknown)>();

	s_OverlayMutex = module.Offset(0x10DB0A38).RCast<LPCRITICAL_SECTION>();

	s_pOverlays = module.Offset(0x10DB0968).RCast<OverlayBase_t**>();

	g_nRenderTickCount = module.Offset(0x10DB0984).RCast<int*>();
	g_nOverlayTickCount = module.Offset(0x10DB0980).RCast<int*>();

	// not in g_pCVar->FindVar by this point for whatever reason, so have to get from memory
	Cvar_enable_debug_overlays = module.Offset(0x10DB0990).RCast<ConVar*>();
	Cvar_enable_debug_overlays->SetValue(false);
	Cvar_enable_debug_overlays->m_pszDefaultValue = (char*)"0";
	Cvar_enable_debug_overlays->AddFlags(FCVAR_CHEAT);
}
