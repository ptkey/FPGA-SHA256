// device interface for connecting cpu to fpga using opencl code.
// initialization based on SDaccel hello world program

#include "device_interface.hpp"
#include "defs.hpp"
#include "xcl2.hpp"


void check(cl_int err) {
  if (err) {
    printf("ERROR: Operation Failed: %d\n", err);
    exit(EXIT_FAILURE);
  }
}


DeviceInterface::DeviceInterface(struct chunk **buffer0, struct chunk **buffer1) {
  host_bufs[0] = buffer0;
  host_bufs[1] = buffer1;
  first_flag = 1;

  // The get_xil_devices will return vector of Xilinx Devices
  std::vector<cl::Device> devices = xcl::get_xil_devices();
  cl::Device device = devices[0];

  //Creating Context and Command Queue for selected Device
  cl::Context context(device);
  q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE);
  std::string device_name = device.getInfo<CL_DEVICE_NAME>();
  std::cout << "Found Device=" << device_name.c_str() << std::endl;

  // import_binary() command will find the OpenCL binary file created using the
  // xocc compiler load into OpenCL Binary and return as Binaries
  // OpenCL and it can contain many functions which can be executed on the
  // device.
  std::string binaryFile = xcl::find_binary_file(device_name, "device_kernel");
  cl::Program::Binaries bins = xcl::import_binary_file(binaryFile);
  devices.resize(1);
  program = cl::Program(context, devices, bins);

  buffer_ext0.flags = XCL_MEM_DDR_BANK0; // Specify Bank0 Memory for input memory
  buffer_ext1.flags = XCL_MEM_DDR_BANK1; // Specify Bank1 Memory for output Memory
  buffer_ext0.obj = NULL;
  buffer_ext1.obj = NULL; // Setting Obj and Param to Zero
  buffer_ext0.param = 0;
  buffer_ext1.param = 0;

  int err;
  ocl_bufs[0] = cl::Buffer(context, CL_MEM_READ_WRITE | CL_MEM_EXT_PTR_XILINX, BUFFER_SIZE, &buffer_ext0, &err);
  if (err != CL_SUCCESS) {
    printf("Error: Failed to allocate buffer0 in DDR bank %zu\n", BUFFER_SIZE);
  }
  ocl_bufs[1] = cl::Buffer(context, CL_MEM_READ_WRITE | CL_MEM_EXT_PTR_XILINX, BUFFER_SIZE, &buffer_ext1, &err);
  if (err != CL_SUCCESS) {
    printf("Error: Failed to allocate buffer1 in DDR bank %zu\n", BUFFER_SIZE);
  }

  *host_bufs[0] = (struct chunk *) q.enqueueMapBuffer(ocl_bufs[0], CL_TRUE, CL_MAP_WRITE, 0, BUFFER_SIZE, NULL, NULL, &err);
  if (err != CL_SUCCESS) {
    printf("Error: Failed to enqueuemapbuffer\n");
  }

  printf("host_bufs[0]: %p\n", *host_bufs[0]);
  *host_bufs[1] = nullptr;

  // This call will extract a kernel out of the program we loaded in the
  // previous line. A kernel is an OpenCL function that is executed on the
  // FPGA. This function is defined in the interface/device_kernel.cl file.
  krnl_sha = cl::Kernel(program, "device_kernel");
}

struct chunk *DeviceInterface::run_fpga(int num_chunks, int active_buf) {
  cl_int err;
  printf("running fpga: %d, %d %p\n", num_chunks, active_buf, *host_bufs[active_buf]);
  // The data will be be transferred from system memory over PCIe to the FPGA on-board
  // DDR memory. blocking.
  q.enqueueUnmapMemObject(ocl_bufs[active_buf], (void *) *host_bufs[active_buf], NULL, NULL);
  res_num_chunks[active_buf] = num_chunks;

  //set the kernel Arguments
  int narg=0;
  krnl_sha.setArg(narg++, ocl_bufs[0]);
  krnl_sha.setArg(narg++, ocl_bufs[1]);
  krnl_sha.setArg(narg++, active_buf);
  krnl_sha.setArg(narg++, num_chunks);

  //Launch the Kernel
  q.enqueueTask(krnl_sha);

  // The result of the previous kernel execution will need to be retrieved in
  // order to view the results. This call will write the data from the
  // buffer_result cl_mem object to the source_results vector
  // first_flag causes us to not read result buffer first time

  // blocking.
  *host_bufs[1 - active_buf] = (struct chunk *) q.enqueueMapBuffer(ocl_bufs[1 - active_buf], CL_TRUE, CL_MAP_WRITE | CL_MAP_READ, 0, BUFFER_SIZE, NULL, NULL, &err);
  check(err);
  res_num_chunks[1 - active_buf] = 0;

  q.finish();

  return *host_bufs[1 - active_buf];
}

void DeviceInterface::read_last_result(int active_buf) {
  // unmap buffer with last result, which will not have input written to it
  q.enqueueUnmapMemObject(ocl_bufs[1 - active_buf], *host_bufs[1 - active_buf]);
  // map last result, such that it can be read.
  *host_bufs[active_buf] = (struct chunk *) q.enqueueMapBuffer(ocl_bufs[active_buf], CL_TRUE, CL_MAP_READ, 0, BUFFER_SIZE);
}

void DeviceInterface::unmap_last_result(int active_buf) {
  q.enqueueUnmapMemObject(ocl_bufs[active_buf], (void *) *host_bufs[active_buf]);
}
