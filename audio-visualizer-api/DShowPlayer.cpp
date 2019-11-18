#include "DShowPlayer.h"

HRESULT DShowPlayer::OpenFile(PCWSTR pszFileName)
{
	IBaseFilter *pSource = NULL;

	// Create a new filter graph. (This also closes the old one, if any.)
	HRESULT hr = InitializeGraph();
	if (FAILED(hr))
	{
		goto done;
	}

	// Add the source filter to the graph.
	hr = m_pGraph->AddSourceFilter(pszFileName, NULL, &pSource);
	if (FAILED(hr))
	{
		goto done;
	}

	// Try to render the streams.
	hr = RenderStreams(pSource);

done:
	if (FAILED(hr))
	{
		TearDownGraph();
	}
	SafeRelease(&pSource);
	return hr;
}

HRESULT DShowPlayer::InitializeGraph()
{
	TearDownGraph();

	// Create the Filter Graph Manager.
	HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pGraph));
	if (FAILED(hr))
	{
		goto done;
	}

	hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pControl));
	if (FAILED(hr))
	{
		goto done;
	}

	hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&m_pEvent));
	if (FAILED(hr))
	{
		goto done;
	}

	// Set up event notification.
	hr = m_pEvent->SetNotifyWindow((OAHWND)m_hwnd, WM_GRAPH_EVENT, NULL);
	if (FAILED(hr))
	{
		goto done;
	}

	m_state = STATE_STOPPED;

done:
	return hr;
}

// Render the streams from a source filter. 

HRESULT DShowPlayer::RenderStreams(IBaseFilter *pSource)
{
	BOOL bRenderedAnyPin = FALSE;

	IFilterGraph2 *pGraph2 = NULL;
	IEnumPins *pEnum = NULL;
	IBaseFilter *pAudioRenderer = NULL;
	HRESULT hr = m_pGraph->QueryInterface(IID_PPV_ARGS(&pGraph2));
	if (FAILED(hr))
	{
		goto done;
	}

	// Add the video renderer to the graph
	hr = CreateVideoRenderer();
	if (FAILED(hr))
	{
		goto done;
	}

	// Add the DSound Renderer to the graph.
	hr = AddFilterByCLSID(m_pGraph, CLSID_DSoundRender,
		&pAudioRenderer, L"Audio Renderer");
	if (FAILED(hr))
	{
		goto done;
	}

	// Enumerate the pins on the source filter.
	hr = pSource->EnumPins(&pEnum);
	if (FAILED(hr))
	{
		goto done;
	}

	// Loop through all the pins
	IPin *pPin;
	while (S_OK == pEnum->Next(1, &pPin, NULL))
	{
		// Try to render this pin. 
		// It's OK if we fail some pins, if at least one pin renders.
		HRESULT hr2 = pGraph2->RenderEx(pPin, AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);

		pPin->Release();
		if (SUCCEEDED(hr2))
		{
			bRenderedAnyPin = TRUE;
		}
	}

	hr = m_pVideo->FinalizeGraph(m_pGraph);
	if (FAILED(hr))
	{
		goto done;
	}

	// Remove the audio renderer, if not used.
	BOOL bRemoved;
	hr = RemoveUnconnectedRenderer(m_pGraph, pAudioRenderer, &bRemoved);

done:
	SafeRelease(&pEnum);
	SafeRelease(&pAudioRenderer);
	SafeRelease(&pGraph2);

	// If we succeeded to this point, make sure we rendered at least one 
	// stream.
	if (SUCCEEDED(hr))
	{
		if (!bRenderedAnyPin)
		{
			hr = VFW_E_CANNOT_RENDER;
		}
	}
	return hr;
}

HRESULT RemoveUnconnectedRenderer(IGraphBuilder *pGraph, IBaseFilter *pRenderer, BOOL *pbRemoved)
{
	IPin *pPin = NULL;

	*pbRemoved = FALSE;

	// Look for a connected input pin on the renderer.

	HRESULT hr = FindConnectedPin(pRenderer, PINDIR_INPUT, &pPin);
	SafeRelease(&pPin);

	// If this function succeeds, the renderer is connected, so we don't remove it.
	// If it fails, it means the renderer is not connected to anything, so
	// we remove it.

	if (FAILED(hr))
	{
		hr = pGraph->RemoveFilter(pRenderer);
		*pbRemoved = TRUE;
	}

	return hr;
}

void DShowPlayer::TearDownGraph()
{
	// Stop sending event messages
	if (m_pEvent)
	{
		m_pEvent->SetNotifyWindow((OAHWND)NULL, NULL, NULL);
	}

	SafeRelease(&m_pGraph);
	SafeRelease(&m_pControl);
	SafeRelease(&m_pEvent);

	delete m_pVideo;
	m_pVideo = NULL;

	m_state = STATE_NO_GRAPH;
}