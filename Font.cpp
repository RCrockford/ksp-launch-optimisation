#include "stdafx.h"
#include "Font.h"
#include "RocketSim.h"

//------------------------------------------------------------------------------------------------

#ifdef _DEBUG
const int s_SuperSample = 4;
#else
const int s_SuperSample = 16;
#endif

static char fontVertexShader[] =
"struct PSInput\n"
"{\n"
"    float4 position : SV_POSITION;\n"
"    float2 texcoord : TEXCOORD;\n"
"    float4 color : COLOR;\n"
"    float2 scale : SCALE;\n"
"};\n"
"\n"
"PSInput VSMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float4 color : COLOR, float2 scale : SCALE)\n"
"{\n"
"    PSInput result;\n"
"\n"
"    result.position = position;\n"
"    result.texcoord = texcoord;\n"
"    result.color = color;\n"
"    result.scale = scale;\n"
"\n"
"    return result;\n"
"}\n"
;

static char fontPixelShader[] =
"struct PSInput\n"
"{\n"
"    float4 position : SV_POSITION;\n"
"    float2 texcoord : TEXCOORD;\n"
"    float4 color : COLOR;\n"
"    float2 scale : SCALE;\n"
"};\n"
"\n"
"Texture2D g_texture : register(t0);\n"
"SamplerState g_sampler : register(s0);\n"
"\n"
"float4 PSMain(PSInput input) : SV_TARGET\n"
"{\n"
"   float3 font = g_texture.Sample(g_sampler, input.texcoord);\n"
"   float alpha = max(min(font.r, font.g), min(max(font.r, font.g), font.b));\n"
"\n"
"   return float4(input.color.rgb, input.color.a * smoothstep(input.scale.x, input.scale.y, alpha));\n"
"}\n"
;

//------------------------------------------------------------------------------------------------

bool Font::Load()
{
    FILE* fp;
    if ( fopen_s( &fp, "font_segoeui.dat", "rb" ) != 0 )
        return false;

    uint32_t s;
    fread( &s, sizeof( uint32_t ), 1, fp );
    fread( &m_cellHeight, sizeof( float ), 1, fp );
    fread( &m_baseline, sizeof( float ), 1, fp );
    fread( &m_padding, sizeof( uint32_t ), 1, fp );

    fread( &m_texWidth, sizeof( uint32_t ), 1, fp );
    fread( &m_texHeight, sizeof( uint32_t ), 1, fp );

    if ( feof(fp) || ferror(fp) || s != m_glyphMetrics.size() )
    {
        fclose( fp );
        return false;
    }

    for ( GlyphMetrics& metric : m_glyphMetrics )
    {
        fread( &metric.upos, sizeof( float ), 1, fp );
        fread( &metric.vpos, sizeof( float ), 1, fp );
        fread( &metric.width, sizeof( float ), 1, fp );

        fread( &s, sizeof( uint32_t ), 1, fp );
        for ( uint32_t i = 0; i < s; ++i )
        {
            wchar_t other;
            float kern;
            fread( &other, sizeof( wchar_t ), 1, fp );
            fread( &kern, sizeof( float ), 1, fp );

            metric.kerningPairs.emplace( other, kern );
        }

        if ( feof( fp ) || ferror( fp ) )
        {
            fclose( fp );
            return false;
        }
    }

    m_textureData = new uint32_t[m_texWidth * m_texWidth];
    size_t r = fread( m_textureData, sizeof( uint32_t ), m_texWidth * m_texHeight, fp );
    fclose( fp );

    return r == m_texWidth * m_texHeight;
}

void Font::Save()
{
    FILE* fp;
    if ( fopen_s( &fp, "font_segoeui.dat", "wb" ) == 0 )
    {
        uint32_t s = static_cast<uint32_t>(m_glyphMetrics.size());
        fwrite( &s, sizeof( uint32_t ), 1, fp );
        fwrite( &m_cellHeight, sizeof( float ), 1, fp );
        fwrite( &m_baseline, sizeof( float ), 1, fp );
        fwrite( &m_padding, sizeof( uint32_t ), 1, fp );

        fwrite( &m_texWidth, sizeof( uint32_t ), 1, fp );
        fwrite( &m_texHeight, sizeof( uint32_t ), 1, fp );

        for ( const GlyphMetrics& metric : m_glyphMetrics )
        {
            fwrite( &metric.upos, sizeof( float ), 1, fp );
            fwrite( &metric.vpos, sizeof( float ), 1, fp );
            fwrite( &metric.width, sizeof( float ), 1, fp );

            s = static_cast<uint32_t>(metric.kerningPairs.size());
            fwrite( &s, sizeof( uint32_t ), 1, fp );
            for ( const std::pair<wchar_t, float>& kp : metric.kerningPairs )
            {
                fwrite( &kp.first, sizeof( wchar_t ), 1, fp );
                fwrite( &kp.second, sizeof( float ), 1, fp );
            }
        }

        fwrite( m_textureData, sizeof( uint32_t ), m_texWidth * m_texHeight, fp );
        fclose( fp );
    }
}

//------------------------------------------------------------------------------------------------

void Font::Create(HWND hwnd, RocketSim* simulation, unsigned height)
{
    m_simulation = simulation;

    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(height);

    if ( !Load() )
    {
        m_freeType = msdfgen::initializeFreetype();
        if ( !m_freeType )
            return;

        wchar_t* fontPath;
        SHGetKnownFolderPath( FOLDERID_Fonts, KF_FLAG_DEFAULT, nullptr, &fontPath );

        char fontFilename[MAX_PATH];
        sprintf_s( fontFilename, "%S\\SegoeUI.ttf", fontPath );

        if ( !GetFileAttributesA( fontFilename ) )
        {
            sprintf_s( fontFilename, "%S\\SegoeUI.otf", fontPath );
        }

        CoTaskMemFree( fontPath );

        m_font = msdfgen::loadFont( m_freeType, fontFilename );
        if ( !m_font )
        {
            msdfgen::deinitializeFreetype( m_freeType );
            m_freeType = nullptr;
            return;
        }

        // text metrics
        msdfgen::Vector2 minBound, maxBound;

        msdfgen::getFontHeight( m_scale, m_font );
        m_scale = height / m_scale;

        double padScale;
        msdfgen::getFontAveWidth( padScale, m_font );

        // padding is half 'm' char width in pixels
        m_padding = uint32_t( (padScale * 0.5) * m_scale + 0.5 );
        m_cellHeight = float( height + m_padding * 2 );

        double descender;
        msdfgen::getFontDescent( descender, m_font );
        m_baseline = float( -descender * m_scale ) - 0.5f;

        // translate is padding in font units
        m_translate.x = padScale * 0.5;
        m_translate.y = padScale * 0.5 - descender;

        double maxWidth;
        msdfgen::getFontMaxWidth( maxWidth, m_font );
        maxWidth *= m_scale;

        // Allow max_size * height for each char (a bit oversized)
        m_texWidth = uint32_t( sqrt( (maxWidth + m_padding * 2) * m_cellHeight * ARRAY_SIZE( m_glyphMetrics ) ) + 3.99f ) & ~3;

        m_textureData = new uint32_t[m_texWidth * m_texWidth];
        memset( m_textureData, 0, m_texWidth * m_texWidth * sizeof( uint32_t ) );

        // Start loading thread
        m_loadThreadHandle = reinterpret_cast<HANDLE>(_beginthreadex( nullptr, 0, Font::loadThreadProc, this, 0, nullptr ));
    }
}

//------------------------------------------------------------------------------------------------

unsigned Font::loadThreadProc(void* data)
{
    static_cast<Font*>(data)->loadData();

    return 0;
}

void Font::loadData()
{
    float currentUPos = 0.0f;
    float currentVPos = 0.0f;

    for ( uint32_t loadGlyph = 0; loadGlyph < ARRAY_SIZE( m_glyphMetrics ); ++loadGlyph )
    {
        wchar_t glyph = wchar_t( loadGlyph + L' ' );
        if ( loadGlyph == m_glyphMetrics.size() - 1 )
        {
            glyph = 0xfffd;
        }

        msdfgen::Shape shape;
        double advance;
        float width = 0.0f;

        if ( glyph == L' ' )
        {
            double tabAdv;
            msdfgen::getFontWhitespaceWidth( advance, tabAdv, m_font );
            width = float( advance * m_scale + m_padding * 2 ) / m_texWidth;
        }
        else if ( msdfgen::loadGlyph( shape, m_font, glyph, &advance ) && shape.validate() )
        {
            shape.inverseYAxis = !shape.inverseYAxis;
            shape.normalize();

            uint_fast32_t iwidth = uint_fast32_t( advance * m_scale + m_padding * 2 + 0.5 );
            width = float( iwidth ) / m_texWidth;

            msdfgen::Bitmap<msdfgen::FloatRGB> msdf( iwidth, int( m_cellHeight ) );

            edgeColoringSimple( shape, 3.0, 0 );
            generateMSDF( msdf, shape, m_padding / 384.0f, m_scale, m_translate, 1.00000001 );

            if ( currentUPos + width > 1.0f )
            {
                currentUPos = 0.0f;
                currentVPos += m_cellHeight;
            }

            uint32_t* dst = m_textureData + (uint32_t( currentVPos ) * m_texWidth) + uint32_t( currentUPos * m_texWidth );

#pragma omp parallel for
            for ( int_fast32_t y = 0; y < msdf.height(); ++y )
            {
                for ( int_fast32_t x = 0; x < msdf.width(); ++x )
                {
                    // scale and bias for texturing
                    msdfgen::FloatRGB dist = msdf( x, y );

                    int_fast32_t r = int( dist.r + 768.5f );
                    int_fast32_t g = int( dist.g + 768.5f );
                    int_fast32_t b = int( dist.b + 768.5f );
                    r = msdfgen::max( 0, msdfgen::min( r, 1023 ) );
                    g = msdfgen::max( 0, msdfgen::min( g, 1023 ) );
                    b = msdfgen::max( 0, msdfgen::min( b, 1023 ) );

                    dst[x] = (r & 0x3ff) | ((g & 0x3ff) << 10) | ((b & 0x3ff) << 20);
                }
                dst += m_texWidth;
            }
        }

        m_glyphMetrics[loadGlyph].upos = currentUPos;
        m_glyphMetrics[loadGlyph].vpos = currentVPos;
        m_glyphMetrics[loadGlyph].width = width;

        // Get kerning pairs for glyph
        for ( wchar_t other = L' '; other < ARRAY_SIZE( m_glyphMetrics ) + L' '; ++other )
        {
            double kernScale = m_scale / m_texWidth;

            double kern;
            if ( msdfgen::getKerning( kern, m_font, glyph, other ) && kern != 0.0 )
            {
                m_glyphMetrics[loadGlyph].kerningPairs.emplace( other, float( kern * kernScale ) );
            }
        }

        if ( glyph != L' ' )
        {
            currentUPos += float( width );
        }
    }

    m_texHeight = uint32_t( currentVPos + m_cellHeight );
    m_cellHeight /= m_texHeight;

    for ( GlyphMetrics& glyphMet : m_glyphMetrics )
    {
        glyphMet.vpos = glyphMet.vpos / m_texHeight;
    }

    Save();
}

//------------------------------------------------------------------------------------------------

void Font::UpdateLoading()
{
    m_currentInst = 0;
    m_currentVert = 0;

    if ( !m_pso && (!m_font || WaitForSingleObject(m_loadThreadHandle, 0) == WAIT_OBJECT_0) )
    {
        if ( !m_texture && m_texHeight > 0 )
        {
            if ( m_font )
                msdfgen::destroyFont( m_font );
            if ( m_freeType )
                msdfgen::deinitializeFreetype( m_freeType );

            {
                DebugTrace( L"Creating font texture: %ux%u", m_texWidth, m_texHeight );

                DXGI_FORMAT texFormat = DXGI_FORMAT_R10G10B10A2_UNORM;

                // Create the texture.
                m_texture = m_simulation->CreateTexture( L"FontTexture", m_textureData, m_texWidth, m_texHeight, texFormat, 4, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false );

                delete[] m_textureData;

                // Describe and create a SRV for the texture.
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = texFormat;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                m_simulation->GetDevice()->CreateShaderResourceView( m_texture.Get(), &srvDesc, m_simulation->GetCPUHandle( ShaderShared::srvFontTexture ) );
            }

            // create vertex buffers
            for ( UINT i = 0; i < ARRAY_SIZE( m_vertexBuffers ); ++i )
            {
                m_vertexBuffers[i] = m_simulation->CreateUploadBuffer( L"FontVerts", nullptr, sizeof( Vertex ) * s_MaxVerts );

                m_vertexBufferViews[i][0].BufferLocation = m_vertexBuffers[i]->GetGPUVirtualAddress();
                m_vertexBufferViews[i][0].StrideInBytes = sizeof( Vertex );
                m_vertexBufferViews[i][0].SizeInBytes = sizeof( Vertex ) * s_MaxVerts;
            }

            // create instance buffers
            for ( UINT i = 0; i < ARRAY_SIZE( m_instanceBuffers ); ++i )
            {
                m_instanceBuffers[i] = m_simulation->CreateUploadBuffer( L"FontInstances", nullptr, sizeof( InstData ) * s_MaxInst );

                m_vertexBufferViews[i][1].BufferLocation = m_instanceBuffers[i]->GetGPUVirtualAddress();
                m_vertexBufferViews[i][1].StrideInBytes = sizeof( InstData );
                m_vertexBufferViews[i][1].SizeInBytes = sizeof( InstData ) * s_MaxInst;
            }

            // create index buffer
            uint16_t indices[s_MaxBatch * 6];

            for ( uint_fast32_t i = 0, v = 0; i < ARRAY_SIZE( indices ); i += 6, v += 4 )
            {
                indices[i] = uint16_t( v );
                indices[i + 1] = uint16_t( v + 1 );
                indices[i + 2] = uint16_t( v + 2 );
                indices[i + 3] = uint16_t( v + 2 );
                indices[i + 4] = uint16_t( v + 1 );
                indices[i + 5] = uint16_t( v + 3 );
            }

            m_indexBuffer = m_simulation->CreateDefaultBuffer( L"FontIndices", indices, sizeof( indices ), D3D12_RESOURCE_STATE_INDEX_BUFFER, false );

            m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
            m_indexBufferView.SizeInBytes = sizeof( indices );
            m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        }
        // delay pso creation one frame to allow index buffer to fill.
        else if ( !m_pso )
        {
            std::array<ComPtr<ID3DBlob>, RocketSim::ShaderType_Count> shaderList;
            ComPtr<ID3DBlob> errors;

            D3DCompile( fontVertexShader, sizeof( fontVertexShader ), "embedded", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &shaderList[RocketSim::ShaderType_Vertex], &errors );
            if ( !shaderList[RocketSim::ShaderType_Vertex] && errors )
            {
                DebugTrace( L"Failed to compile vertex shader: %S", errors->GetBufferPointer() );
            }
            D3DCompile( fontPixelShader, sizeof( fontPixelShader ), "embedded", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &shaderList[RocketSim::ShaderType_Pixel], &errors );
            if ( !shaderList[RocketSim::ShaderType_Pixel] && errors )
            {
                DebugTrace( L"Failed to compile pixel shader: %S", errors->GetBufferPointer() );
            }

            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

                { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 0 },
                { "SCALE", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 4, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 0 },
            };

            m_pso = m_simulation->CreateGraphicsPSO( L"FontPSO", inputElementDescs, ARRAY_SIZE( inputElementDescs ), shaderList,
                                                     D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D12_CULL_MODE_BACK, m_simulation->s_blendedRenderTargetBlendDesc );

            CloseHandle( m_loadThreadHandle );
        }
    }
}

//------------------------------------------------------------------------------------------------

void Font::DrawText(float x, float y, float scale, float angle, uint32_t colour, const wchar_t* str)
{
    if (m_pso && m_currentVert < s_MaxVerts)
    {
        x *= m_simulation->GetViewport().Width;
        y *= m_simulation->GetViewport().Height;

        float xRotCentre = x;
        float yRotCentre = y;

        // scale factors to go from UV space to screen pixels
        float xScale = scale * 2.0f * m_texWidth;
        float yScale = scale * 2.0f * m_texHeight;

        float xPadding = xScale * m_padding / m_texWidth;
        float yPadding = yScale * m_padding / m_texHeight;

        x -= xPadding;
        y -= yPadding + m_baseline * yScale / m_texHeight;

        Vertex* vtxData;
        D3D12_RANGE readRange = { 0 };
        if (FAILED(m_vertexBuffers[m_simulation->GetFrameIndex()]->Map(0, &readRange, reinterpret_cast<void**>(&vtxData))))
            return;

        D3D12_RANGE writeRange = { m_currentVert * sizeof(Vertex), 0 };
        uint32_t startVertex = m_currentVert;

        uint_fast32_t glyphCount = 0;
        for (const wchar_t* s = str; *s && m_currentVert < s_MaxVerts; ++s)
        {
            uint_fast32_t glyphIndex = *s - L' ';
            glyphIndex = msdfgen::min(glyphIndex, uint_fast32_t(ARRAY_SIZE(m_glyphMetrics) - 1));

            const GlyphMetrics& glyph = m_glyphMetrics[glyphIndex];

            if (glyphIndex > 0)
            {
                vtxData[m_currentVert].x = x;
                vtxData[m_currentVert].y = y;
                vtxData[m_currentVert].z = 0.0f;
                vtxData[m_currentVert].u = glyph.upos;
                vtxData[m_currentVert].v = glyph.vpos + m_cellHeight;
                ++m_currentVert;

                vtxData[m_currentVert].x = x;
                vtxData[m_currentVert].y = y + m_cellHeight * yScale;
                vtxData[m_currentVert].z = 0.0f;
                vtxData[m_currentVert].u = glyph.upos;
                vtxData[m_currentVert].v = glyph.vpos;
                ++m_currentVert;

                vtxData[m_currentVert].x = x + glyph.width * xScale;
                vtxData[m_currentVert].y = y;
                vtxData[m_currentVert].z = 0.0f;
                vtxData[m_currentVert].u = glyph.upos + glyph.width;
                vtxData[m_currentVert].v = glyph.vpos + m_cellHeight;
                ++m_currentVert;

                vtxData[m_currentVert].x = x + glyph.width * xScale;
                vtxData[m_currentVert].y = y + m_cellHeight * yScale;
                vtxData[m_currentVert].z = 0.0f;
                vtxData[m_currentVert].u = glyph.upos + glyph.width;
                vtxData[m_currentVert].v = glyph.vpos;
                ++m_currentVert;

                ++glyphCount;
            }

            x += glyph.width * xScale - xPadding * 2;

            std::map<wchar_t, float>::const_iterator kern = glyph.kerningPairs.find(s[1]);
            if (kern != glyph.kerningPairs.end())
            {
                x += kern->second * xScale;
            }
        }

        if (angle != 0.0f)
        {
            float s = sin( angle * 3.14159265f / 180.0f );
            float c = cos( angle * 3.14159265f / 180.0f );

            for ( uint_fast32_t i = startVertex; i < m_currentVert; ++i )
            {
                x = vtxData[i].x - xRotCentre;
                y = vtxData[i].y - yRotCentre;

                vtxData[i].x = x * c - y * s + xRotCentre;
                vtxData[i].y = x * s + y * c + yRotCentre;
            }
        }

        // scale to viewport.
        xScale = 1.0f / m_simulation->GetViewport().Width;
        yScale = 1.0f / m_simulation->GetViewport().Height;
        for ( uint_fast32_t i = startVertex; i < m_currentVert; ++i )
        {
            vtxData[i].x = vtxData[i].x * xScale;
            vtxData[i].y = vtxData[i].y * yScale;
        }

        InstData* instData;
        if (FAILED(m_instanceBuffers[m_simulation->GetFrameIndex()]->Map(0, &readRange, reinterpret_cast<void**>(&instData))))
            return;

        writeRange.Begin = m_currentInst * sizeof(InstData);

        instData[m_currentInst].colour = colour;
        instData[m_currentInst].minscale = 0.75f - 0.008f / scale;
        instData[m_currentInst].maxscale = 0.75f + 0.008f / scale;
        ++m_currentInst;

        writeRange.End = m_currentInst * sizeof(InstData);

        m_instanceBuffers[m_simulation->GetFrameIndex()]->Unmap(0, &writeRange);

        m_simulation->GetCommandList()->SetPipelineState(m_pso.Get());

        m_simulation->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_simulation->GetCommandList()->IASetIndexBuffer(&m_indexBufferView);
        m_simulation->GetCommandList()->IASetVertexBuffers(0, 2, m_vertexBufferViews[m_simulation->GetFrameIndex()]);
        
        m_simulation->GetCommandList()->DrawIndexedInstanced(glyphCount * 6, 1, 0, startVertex, UINT(writeRange.Begin / sizeof(InstData)));
    }
}

//------------------------------------------------------------------------------------------------

float Font::CalcTextLength(const wchar_t* str)
{
    // scale factors to go from UV space to screen pixels
    float xScale = 2.0f * m_texWidth;
    float xPadding = xScale * m_padding / m_texWidth;

    float width = 0.0f;

    for (const wchar_t* s = str; *s; ++s)
    {
        uint_fast32_t glyphIndex = *s - L' ';
        glyphIndex = msdfgen::min(glyphIndex, uint_fast32_t(ARRAY_SIZE(m_glyphMetrics) - 1));

        const GlyphMetrics& glyph = m_glyphMetrics[glyphIndex];

        width += glyph.width * xScale - xPadding * 2;

        std::map<wchar_t, float>::const_iterator kern = glyph.kerningPairs.find(s[1]);
        if (kern != glyph.kerningPairs.end())
        {
            width += kern->second * xScale;
        }
    }

    return width / m_simulation->GetViewport().Width;
}

