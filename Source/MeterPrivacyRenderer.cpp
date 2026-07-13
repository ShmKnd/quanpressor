#include "MeterPrivacyRenderer.h"

#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <cmath>

namespace
{
    class OpenGLGaussianBlurRenderer final : public MeterPrivacyRenderer,
                                              private juce::OpenGLRenderer
    {
    public:
        OpenGLGaussianBlurRenderer()
        {
            view.setOpaque(false);
            view.setInterceptsMouseClicks(false, false);
            view.setVisible(false);

            context.setComponentPaintingEnabled(false);
            context.setContinuousRepainting(false);
            context.setMultisamplingEnabled(false);
            context.setRenderer(this);
        }

        ~OpenGLGaussianBlurRenderer() override
        {
            shutdown();
        }

        const char* getBackendName() const noexcept override
        {
            return "OpenGL Separable Gaussian Blur";
        }

        juce::Component& getView() noexcept override
        {
            return view;
        }

        void attach() override
        {
            if (!context.isAttached())
                context.attachTo(view);
        }

        void shutdown() override
        {
            enabled.store(false, std::memory_order_release);
            if (context.isAttached())
                context.detach();
        }

        void setEnabled(bool shouldBeEnabled) override
        {
            enabled.store(shouldBeEnabled, std::memory_order_release);
            view.setVisible(shouldBeEnabled);
            if (shouldBeEnabled && context.isAttached())
                context.triggerRepaint();
        }

        void submitFrame(juce::Image source, Regions newRegions, float newBlurRadius,
                         juce::Image foreground) override
        {
            if (!source.isValid())
                return;

            {
                const juce::ScopedLock lock(frameLock);
                pendingSource = std::move(source);
                pendingForeground = std::move(foreground);
                pendingRegions = newRegions;
                pendingBlurRadius = juce::jlimit(1.0f, 80.0f, newBlurRadius);
                ++pendingGeneration;
            }

            if (context.isAttached())
                context.triggerRepaint();
        }

    private:
        struct Vertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
        };

        juce::Component view;
        juce::OpenGLContext context;
        juce::CriticalSection frameLock;
        juce::Image pendingSource;
        juce::Image pendingForeground;
        Regions pendingRegions {};
        float pendingBlurRadius = 8.0f;
        uint64_t pendingGeneration = 0;
        uint64_t uploadedGeneration = 0;
        std::atomic<bool> enabled { false };

        juce::OpenGLTexture sourceTexture;
        juce::OpenGLTexture foregroundTexture;
        juce::OpenGLFrameBuffer horizontalBuffer;
        std::unique_ptr<juce::OpenGLShaderProgram> blurShader;
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> positionAttribute;
        std::unique_ptr<juce::OpenGLShaderProgram::Attribute> textureAttribute;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> sourceUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> texelStepUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> radiusUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> darkenUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> maskBoundsUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> maskFeatherUniform;
        std::unique_ptr<juce::OpenGLShaderProgram::Uniform> maskEnabledUniform;
        GLuint vertexBuffer = 0;

        void newOpenGLContextCreated() override
        {
            static constexpr const char* vertexShader =
                "attribute vec2 position;\n"
                "attribute vec2 textureCoordinate;\n"
                "varying vec2 vTextureCoordinate;\n"
                "void main()\n"
                "{\n"
                "    gl_Position = vec4(position, 0.0, 1.0);\n"
                "    vTextureCoordinate = textureCoordinate;\n"
                "}\n";

            static constexpr const char* fragmentShader =
                "uniform sampler2D sourceTexture;\n"
                "uniform vec2 texelStep;\n"
                "uniform float radius;\n"
                "uniform float darken;\n"
                "uniform vec4 maskBounds;\n"
                "uniform vec2 maskFeather;\n"
                "uniform float maskEnabled;\n"
                "varying vec2 vTextureCoordinate;\n"
                "void main()\n"
                "{\n"
                "    vec4 sum = vec4(0.0);\n"
                "    float samples = 0.0;\n"
                "    float sigma = max(radius * 0.42, 1.0);\n"
                "    float sampleStride = max(radius / 24.0, 1.0);\n"
                "    for (int i = -24; i <= 24; ++i)\n"
                "    {\n"
                "        float fi = float(i);\n"
                "        float distance = fi * sampleStride;\n"
                "        float weight = exp(-0.5 * distance * distance / (sigma * sigma));\n"
                "        sum += texture2D(sourceTexture, vTextureCoordinate + texelStep * distance) * weight;\n"
                "        samples += weight;\n"
                "    }\n"
                "    vec4 colour = sum / max(samples, 1.0);\n"
                "    colour.rgb = mix(colour.rgb, vec3(0.035), darken);\n"
                "    if (maskEnabled > 0.5)\n"
                "    {\n"
                "        vec2 edgeDistance = min(vTextureCoordinate - maskBounds.xy,\n"
                "                                maskBounds.zw - vTextureCoordinate);\n"
                "        vec2 edgeAlpha = smoothstep(vec2(0.0), maskFeather, edgeDistance);\n"
                "        colour.a *= edgeAlpha.x * edgeAlpha.y;\n"
                "    }\n"
                "    gl_FragColor = colour;\n"
                "}\n";

            blurShader = std::make_unique<juce::OpenGLShaderProgram>(context);
            if (blurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(vertexShader))
                && blurShader->addFragmentShader(juce::OpenGLHelpers::translateFragmentShaderToV3(fragmentShader))
                && blurShader->link())
            {
                positionAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*blurShader, "position");
                textureAttribute = std::make_unique<juce::OpenGLShaderProgram::Attribute>(*blurShader, "textureCoordinate");
                sourceUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "sourceTexture");
                texelStepUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "texelStep");
                radiusUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "radius");
                darkenUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "darken");
                maskBoundsUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "maskBounds");
                maskFeatherUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "maskFeather");
                maskEnabledUniform = std::make_unique<juce::OpenGLShaderProgram::Uniform>(*blurShader, "maskEnabled");
            }
            else
            {
                blurShader.reset();
            }

            context.extensions.glGenBuffers(1, &vertexBuffer);
        }

        void openGLContextClosing() override
        {
            sourceTexture.release();
            foregroundTexture.release();
            horizontalBuffer.release();

            if (vertexBuffer != 0)
            {
                context.extensions.glDeleteBuffers(1, &vertexBuffer);
                vertexBuffer = 0;
            }

            maskEnabledUniform.reset();
            maskFeatherUniform.reset();
            maskBoundsUniform.reset();
            darkenUniform.reset();
            radiusUniform.reset();
            texelStepUniform.reset();
            sourceUniform.reset();
            textureAttribute.reset();
            positionAttribute.reset();
            blurShader.reset();
            uploadedGeneration = 0;
        }

        void uploadPendingFrame()
        {
            juce::Image source;
            juce::Image foreground;
            uint64_t generation = 0;
            {
                const juce::ScopedLock lock(frameLock);
                generation = pendingGeneration;
                if (generation == uploadedGeneration)
                    return;
                source = pendingSource;
                foreground = pendingForeground;
            }

            if (!source.isValid())
                return;

            sourceTexture.loadImage(source);
            if (foreground.isValid())
                foregroundTexture.loadImage(foreground);
            else
                foregroundTexture.release();
            const int bufferWidth = juce::jmax(1, source.getWidth());
            const int bufferHeight = juce::jmax(1, source.getHeight());
            if (!horizontalBuffer.isValid()
                || horizontalBuffer.getWidth() != bufferWidth
                || horizontalBuffer.getHeight() != bufferHeight)
            {
                horizontalBuffer.release();
                horizontalBuffer.initialise(context, bufferWidth, bufferHeight);
            }

            uploadedGeneration = generation;
        }

        void renderOpenGL() override
        {
            const float scale = static_cast<float>(context.getRenderingScale());
            const int targetWidth = juce::roundToInt(static_cast<float>(view.getWidth()) * scale);
            const int targetHeight = juce::roundToInt(static_cast<float>(view.getHeight()) * scale);
            if (targetWidth <= 0 || targetHeight <= 0)
                return;

            juce::gl::glViewport(0, 0, targetWidth, targetHeight);
            juce::gl::glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            juce::gl::glClear(juce::gl::GL_COLOR_BUFFER_BIT);

            if (!enabled.load(std::memory_order_acquire))
                return;

            uploadPendingFrame();
            if (blurShader == nullptr || vertexBuffer == 0 || sourceTexture.getTextureID() == 0
                || !horizontalBuffer.isValid())
                return;

            Regions regions;
            float blurRadius = 8.0f;
            {
                const juce::ScopedLock lock(frameLock);
                regions = pendingRegions;
                blurRadius = pendingBlurRadius;
            }

            juce::gl::glDisable(juce::gl::GL_DEPTH_TEST);
            juce::gl::glDisable(juce::gl::GL_BLEND);

            const int sourceWidth = juce::jmax(1, sourceTexture.getWidth());
            const int sourceHeight = juce::jmax(1, sourceTexture.getHeight());
            const auto fullArea = juce::Rectangle<int>(0, 0, view.getWidth(), view.getHeight());

            horizontalBuffer.makeCurrentAndClear();
            juce::gl::glViewport(0, 0, horizontalBuffer.getWidth(), horizontalBuffer.getHeight());
            drawArea(fullArea, sourceTexture.getTextureID(),
                     1.0f / static_cast<float>(sourceWidth), 0.0f, blurRadius, 0.0f, 0.0f);
            horizontalBuffer.releaseAsRenderingTarget();

            juce::gl::glViewport(0, 0, targetWidth, targetHeight);
            juce::gl::glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            juce::gl::glClear(juce::gl::GL_COLOR_BUFFER_BIT);
            juce::gl::glEnable(juce::gl::GL_BLEND);
            juce::gl::glBlendFunc(juce::gl::GL_SRC_ALPHA, juce::gl::GL_ONE_MINUS_SRC_ALPHA);

            // Some plugin hosts composite a transparent OpenGL child as black.
            // Preserve the controls inside this view with the unblurred snapshot,
            // then replace only the requested meter regions with the blurred pass.
            drawArea(fullArea, sourceTexture.getTextureID(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

            for (auto region : regions)
            {
                region = region.getIntersection(fullArea);
                if (!region.isEmpty())
                    drawArea(region, horizontalBuffer.getTextureID(), 0.0f,
                             1.0f / static_cast<float>(juce::jmax(1, horizontalBuffer.getHeight())),
                             blurRadius, 0.16f, 14.0f);
            }

            // Status overlays are intentionally composited after the privacy
            // blur so they remain legible while the meters stay obscured.
            if (foregroundTexture.getTextureID() != 0)
                drawArea(fullArea, foregroundTexture.getTextureID(),
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

            context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);
            juce::gl::glBindTexture(juce::gl::GL_TEXTURE_2D, 0);
            juce::gl::glDisable(juce::gl::GL_BLEND);
        }

        void drawArea(juce::Rectangle<int> area, GLuint textureId,
                      float texelStepX, float texelStepY, float blurRadius, float darken,
                      float edgeFeather)
        {
            if (positionAttribute == nullptr || textureAttribute == nullptr
                || sourceUniform == nullptr || texelStepUniform == nullptr
                || radiusUniform == nullptr || darkenUniform == nullptr
                || maskBoundsUniform == nullptr || maskFeatherUniform == nullptr
                || maskEnabledUniform == nullptr)
                return;

            const float width = static_cast<float>(juce::jmax(1, view.getWidth()));
            const float height = static_cast<float>(juce::jmax(1, view.getHeight()));
            const float left = static_cast<float>(area.getX()) / width;
            const float right = static_cast<float>(area.getRight()) / width;
            const float top = static_cast<float>(area.getY()) / height;
            const float bottom = static_cast<float>(area.getBottom()) / height;

            const float x0 = left * 2.0f - 1.0f;
            const float x1 = right * 2.0f - 1.0f;
            const float y0 = 1.0f - top * 2.0f;
            const float y1 = 1.0f - bottom * 2.0f;
            const float u0 = left;
            const float u1 = right;
            const float v0 = 1.0f - top;
            const float v1 = 1.0f - bottom;

            const std::array<Vertex, 6> vertices {{
                { x0, y0, u0, v0 }, { x0, y1, u0, v1 }, { x1, y0, u1, v0 },
                { x1, y0, u1, v0 }, { x0, y1, u0, v1 }, { x1, y1, u1, v1 }
            }};

            blurShader->use();
            sourceUniform->set(0);
            texelStepUniform->set(texelStepX, texelStepY);
            radiusUniform->set(blurRadius);
            darkenUniform->set(darken);
            maskBoundsUniform->set(left, 1.0f - bottom, right, 1.0f - top);
            maskFeatherUniform->set(edgeFeather / width, edgeFeather / height);
            maskEnabledUniform->set(edgeFeather > 0.0f ? 1.0f : 0.0f);

            juce::gl::glActiveTexture(juce::gl::GL_TEXTURE0);
            juce::gl::glBindTexture(juce::gl::GL_TEXTURE_2D, textureId);
            juce::gl::glTexParameteri(juce::gl::GL_TEXTURE_2D, juce::gl::GL_TEXTURE_MIN_FILTER, juce::gl::GL_LINEAR);
            juce::gl::glTexParameteri(juce::gl::GL_TEXTURE_2D, juce::gl::GL_TEXTURE_MAG_FILTER, juce::gl::GL_LINEAR);
            juce::gl::glTexParameteri(juce::gl::GL_TEXTURE_2D, juce::gl::GL_TEXTURE_WRAP_S, juce::gl::GL_CLAMP_TO_EDGE);
            juce::gl::glTexParameteri(juce::gl::GL_TEXTURE_2D, juce::gl::GL_TEXTURE_WRAP_T, juce::gl::GL_CLAMP_TO_EDGE);

            context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, vertexBuffer);
            context.extensions.glBufferData(juce::gl::GL_ARRAY_BUFFER,
                                            static_cast<GLsizeiptr>(sizeof(vertices)),
                                            vertices.data(), juce::gl::GL_STREAM_DRAW);
            context.extensions.glEnableVertexAttribArray(static_cast<GLuint>(positionAttribute->attributeID));
            context.extensions.glVertexAttribPointer(static_cast<GLuint>(positionAttribute->attributeID), 2,
                                                     juce::gl::GL_FLOAT, juce::gl::GL_FALSE,
                                                     sizeof(Vertex), nullptr);
            context.extensions.glEnableVertexAttribArray(static_cast<GLuint>(textureAttribute->attributeID));
            context.extensions.glVertexAttribPointer(static_cast<GLuint>(textureAttribute->attributeID), 2,
                                                     juce::gl::GL_FLOAT, juce::gl::GL_FALSE,
                                                     sizeof(Vertex), reinterpret_cast<const void*>(sizeof(float) * 2));

            juce::gl::glDrawArrays(juce::gl::GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

            context.extensions.glDisableVertexAttribArray(static_cast<GLuint>(textureAttribute->attributeID));
            context.extensions.glDisableVertexAttribArray(static_cast<GLuint>(positionAttribute->attributeID));
        }
    };
}

std::unique_ptr<MeterPrivacyRenderer> createMeterPrivacyRenderer()
{
    return std::make_unique<OpenGLGaussianBlurRenderer>();
}
