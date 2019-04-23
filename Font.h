#pragma once

#include "msdfgen-master/msdfgen.h"
#include "msdfgen-master/ext/import-font.h"

using Microsoft::WRL::ComPtr;
class RocketSim;

//------------------------------------------------------------------------------------------------

class Font
{
public:
    Font() : m_freeType(nullptr), m_font(nullptr) {}

    void    Create(HWND hwnd, RocketSim* simulation, unsigned height);

    void    UpdateLoading();

    void    DrawText(float x, float y, float scale, float angle, uint32_t colour, const wchar_t* str);
    float   CalcTextLength(const wchar_t* str);

    float   GetLineSpacing() const
    {
        return m_cellHeight - float(m_padding * 2) / m_texHeight;
    }

    float   GetBaseline() const
    {
        return m_baseline / m_texHeight;
    }

    float   GetTopAlign() const
    {
        return m_cellHeight - (float(m_padding * 2) + m_baseline) / m_texHeight;
    }

private:
    struct GlyphMetrics
    {
        float   upos;
        float   vpos;
        float   width;

        std::map<wchar_t, float>   kerningPairs;
    };

    struct Vertex
    {
        float       x, y, z;
        float       u, v;
    };

    struct InstData
    {
        uint32_t    colour;
        float       minscale, maxscale;
    };

    struct Point
    {
        int32_t dx;
        int32_t dy;

        int32_t DistSq() const
        {
            return dx*dx + dy*dy;
        }
    };

    bool Load();
    void Save();

    static unsigned loadThreadProc( void* data );
    void loadData();

    static const uint32_t   s_MaxInst  = 100;
    static const uint32_t   s_MaxBatch = 4096;              // in quads
    static const uint32_t   s_MaxVerts = s_MaxBatch * 4;    // at least s_MaxBatch * 4

    RocketSim*      m_simulation;

    std::array<GlyphMetrics, 256 - 32>  m_glyphMetrics;
    float           m_cellHeight;
    float           m_baseline;
    uint32_t        m_padding;

    ComPtr<ID3D12PipelineState> m_pso;

    ComPtr<ID3D12Resource>      m_texture;

    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW     m_indexBufferView;

    ComPtr<ID3D12Resource>      m_vertexBuffers[2];
    ComPtr<ID3D12Resource>      m_instanceBuffers[2];
    D3D12_VERTEX_BUFFER_VIEW    m_vertexBufferViews[2][2];

    uint32_t    m_currentInst;
    uint32_t    m_currentVert;

    // Loading Data
    msdfgen::FreetypeHandle*    m_freeType;
    msdfgen::FontHandle*        m_font;
    double                      m_scale;
    msdfgen::Vector2            m_translate;

    uint32_t*   m_textureData;
    uint32_t    m_texWidth;
    uint32_t    m_texHeight;
    
    HANDLE      m_loadThreadHandle;
};
