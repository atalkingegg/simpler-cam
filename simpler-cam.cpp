//################################################################################//

#include <iostream>
#include <sys/mman.h> // PROT_READ mmap stuff
#include <semaphore.h> // sem_init - Note: from libpthread
#include <libcamera/libcamera.h>
#include <png.h>

//################################################################################//

//#define X_RES 4056
//#define Y_RES 3040
// 2.3 seconds!

//#define X_RES 3040
//#define Y_RES 3040
// 1.9 seconds!

//#define X_RES 2048
//#define Y_RES 2048
// 1.4 seconds!

#define X_RES 2028
#define Y_RES 1520
// 1.15 seconds!

//#define X_RES 1520
//#define Y_RES 1520
// 1.05 seconds!

//#define X_RES 1024
//#define Y_RES 1024
// 0.92 seconds!

//################################################################################//
//## Callback stuff, wakes up main thread when camera has data ready

sem_t my_sem;

libcamera::Request *gotRequest;

static void requestComplete(libcamera::Request *request)
{
	gotRequest = request;
	sem_post(&my_sem);
}

//################################################################################//
//## based on libcamera-apps/image/png.cpp

void png_save(std::vector<libcamera::Span<uint8_t>> const &mem,
	      libcamera::StreamConfiguration const &streamConfig,
	      std::string const &filename)
{
	if (streamConfig.pixelFormat != libcamera::formats::BGR888)
		throw std::runtime_error("pixel format for png should be BGR");

	FILE *fp = filename == "-" ? stdout : fopen(filename.c_str(), "wb");
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;

	if (fp == NULL)
		throw std::runtime_error("failed to open file " + filename);

	try
	{
		// Open everything up.
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (png_ptr == NULL)
			throw std::runtime_error("failed to create png write struct");

		info_ptr = png_create_info_struct(png_ptr);
		if (info_ptr == NULL)
			throw std::runtime_error("failed to create png info struct");

		if (setjmp(png_jmpbuf(png_ptr)))
			throw std::runtime_error("failed to set png error handling");

		// Set image attributes.
		png_set_IHDR(png_ptr, info_ptr, streamConfig.size.width, streamConfig.size.height,
			 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
			 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		// These settings get us most of the compression, but are much faster.
		png_set_filter(png_ptr, 0, PNG_FILTER_AVG);
		png_set_compression_level(png_ptr, 1);

		// Set up the image data.
		png_byte **row_ptrs = (png_byte **) png_malloc(png_ptr, streamConfig.size.height * sizeof(png_byte *));
		png_byte *row = (uint8_t *)mem[0].data();
		for (unsigned int i = 0; i < streamConfig.size.height; i++, row += streamConfig.stride)
			row_ptrs[i] = row;

		png_init_io(png_ptr, fp);
		png_set_rows(png_ptr, info_ptr, row_ptrs);
		png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

		//#if (options->verbose)
		//{
		long int size = ftell(fp);
		std::cerr << "Wrote PNG file of " << size << " bytes" << std::endl;
		//}

		// Free and close everything and we're done.
		png_free(png_ptr, row_ptrs);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		if (fp != stdout)
			fclose(fp);
	}
	catch (std::exception const &e)
	{
		if (png_ptr)
			png_destroy_write_struct(&png_ptr, &info_ptr);
		if (fp && fp != stdout)
			fclose(fp);
		throw;
	}
}

//################################################################################//

int main()
{
	// setting up semaphore to later wait (sleep) and wake up again on callback.
	// note: starting on zero (last var), so first call to sem_wait will wait until sem_post!
	if (sem_init(&my_sem, 0, 0) != 0)
	{
		std::cout << "Error: semaphore not initialized!" << std::endl;
		return EXIT_FAILURE;
	}

	// Camera Manager

	libcamera::CameraManager *cm = new libcamera::CameraManager();
	cm->start();

	for (auto const &camera : cm->cameras())
		std::cout << "Camera:" << camera->id() << std::endl;

	// Configuration

	std::string cameraId = cm->cameras()[0]->id();

	static std::shared_ptr<libcamera::Camera> camera;

	camera = cm->get(cameraId);

	camera->acquire();

	std::unique_ptr<libcamera::CameraConfiguration> config = camera->generateConfiguration({ libcamera::StreamRole::StillCapture });

	// Change Settings and Validation

	libcamera::StreamConfiguration &streamConfig = config->at(0);

	std::cout << "Default StillCapture configuration: " << streamConfig.toString() << std::endl;

	streamConfig.size.width = X_RES;
	streamConfig.size.height = Y_RES;
	streamConfig.pixelFormat = libcamera::formats::BGR888; // PNG likes only BGR order!
	streamConfig.colorSpace = libcamera::ColorSpace::Rec709; // Note: JPEG likes JPEG colorspace

	//streamConfig.bufferCount = 3; //### This doesn't work. Sets up double/triple buffering.
	//Note: if you set this, you'll get two extra failed callbacks!

	config->validate(); // TODO: check return value!! could be Adjusted or Invalid

	std::cout << "Validated StillCapture configuration: " << streamConfig.toString() << std::endl;

	// Apply configuration

	camera->configure(config.get());

	// Framebuffer Stuffs

	libcamera::FrameBufferAllocator *allocator = new libcamera::FrameBufferAllocator(camera);

	for (libcamera::StreamConfiguration &cfg : *config)
	{
		int ret = allocator->allocate(cfg.stream());
		if (ret < 0) {
			std::cerr << "\nERROR: Can't allocate buffers" << std::endl;
			return EXIT_FAILURE;
		}
		unsigned int allocated = allocator->buffers(cfg.stream()).size();

		std::cout << "Allocated: " << allocated << " buffers for stream." << std::endl;
	}

	// Other Framebuffer Stuffs??

	libcamera::Stream *stream = streamConfig.stream();

	std::map<libcamera::FrameBuffer *, std::vector<libcamera::Span<uint8_t>>> mapped_buffers_;

	// see simple-cam.cpp line 333
	// see core/libcamera_app.cpp line 653
	const std::vector<std::unique_ptr<libcamera::FrameBuffer>> &buffers = allocator->buffers(stream);

	for (const std::unique_ptr<libcamera::FrameBuffer> &buffer : buffers)
	{
		size_t buffer_size = 0;
		for (unsigned i = 0; i < buffer->planes().size(); i++)
		{
			const libcamera::FrameBuffer::Plane &plane = buffer->planes()[i];
			buffer_size += plane.length;
			if (i == buffer->planes().size() - 1 ||
			    plane.fd.get() != buffer->planes()[i + 1].fd.get())
			{
				void *memory = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, plane.fd.get(), 0);
				mapped_buffers_[buffer.get()].push_back(
					libcamera::Span<uint8_t>(static_cast<uint8_t *>(memory), buffer_size));
				buffer_size = 0;
			}
		}
		// frame_buffers_[stream].push(buffer.get());
	}


	std::cout << "Allocated: " << buffers.size() << " buffers for requests." << std::endl;

	std::vector<std::unique_ptr<libcamera::Request>> requests;

	// ## Note: this only works when there's a buffer size of 1.
	// for (unsigned int i = 0; i < buffers.size(); ++i) {
	unsigned int i = 0;

	std::unique_ptr<libcamera::Request> request = camera->createRequest();
	if (!request)
	{
		std::cerr << "Can't create request" << std::endl;
		return EXIT_FAILURE;
	}

	const std::unique_ptr<libcamera::FrameBuffer> &buffer = buffers[i];
	int ret = request->addBuffer(stream, buffer.get());
	if (ret < 0)
	{
		std::cerr << "Can't set buffer for request"
			  << std::endl;
		return EXIT_FAILURE;
	}

	/*
	 * Controls can be added to a request on a per frame basis.
	 * We pass directly to camera.start() then wipe it so it's only set once!
	 */
	libcamera::ControlList &controls = request->controls();
	controls.set(libcamera::controls::Brightness, 0.0); // -1.0 to 1.0
	controls.set(libcamera::controls::Contrast, 1.0); // 0.0 to 15.99
	controls.set(libcamera::controls::AnalogueGain, 1.0);

	//controls.set(libcamera::controls::DigitalGain, 1.0);
	//## Note: Control 0x00000017 not valid for imx477, a bug perhaps?

	controls.set(libcamera::controls::ExposureTime, 40000);
	float rb_gains[] = { 1.2, 1.2 };
	controls.set(libcamera::controls::ColourGains, rb_gains);

	requests.push_back(std::move(request));

	// } // see buffers.size for loop.

	// callback and fire up the camera!

	camera->requestCompleted.connect(requestComplete);

	camera->start(&controls);

	controls.clear();

	for (std::unique_ptr<libcamera::Request> &request : requests)
		camera->queueRequest(request.get());

	// use semaphore to wait-sleep until callback runs.
	sem_wait(&my_sem);

	// Process the data!

	if (gotRequest->status() == libcamera::Request::RequestComplete)
	{
	    const libcamera::Request::BufferMap &buffers = gotRequest->buffers();

	    for (auto bufferPair : buffers)
	    {
		// (Unused) Stream *stream = bufferPair.first;
		libcamera::FrameBuffer *buffer = bufferPair.second;
		const libcamera::FrameMetadata &metadata = buffer->metadata();

		unsigned int nplane = 0;
		std::cout << "buffer size[s]:   ";
		for (const libcamera::FrameMetadata::Plane &plane : metadata.planes())
		{
			std::cout << plane.bytesused;
			if (++nplane < metadata.planes().size())
				std::cout << "/";
		}
		std::cout << std::endl;

		/*
		 * Image data saved out here.
		 */

		png_save(mapped_buffers_.find(buffer)->second, streamConfig, "test.png");

	    }
	} else {
		std::cout << "Error: Camera returned without data!" << std::endl;
	}


	// ## Re-queue the Request to the camera.
	// ## Important for video, not so much here.
	// request->reuse(Request::ReuseBuffers);
	// camera->queueRequest(request);

	camera->stop();

	// see libcamera_app.cpp line 367
	for (auto &iter : mapped_buffers_)
	{
		// assert(iter.first->planes().size() == iter.second.size());
		// for (unsigned i = 0; i < iter.first->planes().size(); i++)
		for (auto &span : iter.second)
			munmap(span.data(), span.size());
	}

	mapped_buffers_.clear();

	allocator->free(stream);
	delete allocator;
	camera->release();
	camera.reset();
	cm->stop();

	return EXIT_SUCCESS;
}

//################################################################################//
