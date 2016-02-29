/*
 * Copyright (c) 2015, Chaos Software Ltd
 *
 * V-Ray For Blender
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VRAY_FOR_BLENDER_EXPORTER_PRODUCTION_H
#define VRAY_FOR_BLENDER_EXPORTER_PRODUCTION_H

#include "vfb_scene_exporter.h"
#include <mutex>
#include <vector>

namespace VRayForBlender {

class ImageBuffer {
	void   allocate(int w, int h) { pixels = new float[w * h]; }
	float *pixels;
};

class RenderImageMan {
};

class ProductionExporter
        : public SceneExporter
{
public:
	ProductionExporter(BL::Context context, BL::RenderEngine engine, BL::BlendData data, BL::Scene scene)
	    : SceneExporter(context, engine, data, scene)
	    , m_renderResult(PointerRNA_NULL)
	    , m_renderFinished(false)
	    , m_isAnimationRunning(false)
	{}

	virtual           ~ProductionExporter() override;

	virtual void      create_exporter() override;

	virtual bool      do_export() override;
	virtual void      sync_dupli(BL::Object ob, const int &check_updated=false) override;
	virtual void      sync_object_modiefiers(BL::Object ob, const int &check_updated);

	virtual void      render_start() override;

	virtual void      setup_callbacks() override;

public:
	void              cb_on_image_ready();
	void              cb_on_rt_image_updated();

private:
	BL::RenderResult  m_renderResult;
	int               m_renderFinished;
	RenderImageMan    m_imageMan;
	volatile bool     m_imageDirty;
	bool              m_isAnimationRunning;
	PyThreadState    *m_animationPythonThreadState;

	std::vector<BL::RenderResult> m_renderResultsList;

	std::mutex        m_callback_mtx;
};

} // namespace VRayForBlender

#endif // VRAY_FOR_BLENDER_EXPORTER_PRODUCTION_H
