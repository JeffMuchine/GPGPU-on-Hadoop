#ifndef KERNEL_PATH
#define KERNEL_PATH "kernel.cl"
#endif

#ifndef __CL_ENABLE_EXCEPTIONS
#define __CL_ENABLE_EXCEPTIONS
#endif

#define X_DIM 0
#define Y_DIM 1

#include "CL/cl.hpp"

#include <iostream>
#include <fstream>
#include <algorithm>

#include "logging.hpp"
#include "timeUtils.hpp"

using namespace logging;

size_t size;
char type;

/*** Kommandozeilenargumente ***/
const char SINGLE = 's';
const char CPU = 'c';
const char GPU = 'g';

const char ALL = 'a';
const char DEBUG = 'd';
const char NORMAL = 'n';
const char ERRORL = 'e';
const char TIMEL = 't';

const logging::Level TIME("TIME", 32);

std::string
readFile(std::string fName)
{
  std::ifstream file;
  file.open(fName.c_str(), std::ios::in);

  if (!file)
    std::cerr << "Could not open file: " << fName << std::endl;

  char c;

  std::string kernelSrc;
  while (file.get(c))
    kernelSrc.push_back(c);
  file.close();
  return kernelSrc;
}

unsigned int
ggt(unsigned int a, unsigned int b)
{
  unsigned int c;
  do
    {
      c = a % b;
      a = b;
      b = c;
    }
  while (c != 0);
  return a;
}

// TODO Berechnung fuer optimale Work-Group-Size optimieren
void
calcWorkGroupSize(size_t dim, size_t* globalSize, size_t* localSize,
    const size_t MAX_GROUP_SIZE)
{
  switch (dim)
    {
  case 1:
    if (globalSize[X_DIM] < localSize[X_DIM])
      localSize[X_DIM] = globalSize[X_DIM];
    else
      localSize[X_DIM] = ggt(globalSize[X_DIM], localSize[X_DIM]);
    return;

  case 2:
    if (globalSize[X_DIM] < localSize[X_DIM])
      localSize[X_DIM] = globalSize[X_DIM];
    if (globalSize[Y_DIM] < localSize[Y_DIM])
      localSize[Y_DIM] = globalSize[Y_DIM];
    while (MAX_GROUP_SIZE < (localSize[X_DIM] * localSize[Y_DIM])
        || (globalSize[X_DIM] % localSize[X_DIM] != 0) || (globalSize[Y_DIM]
        % localSize[Y_DIM] != 0))
      {
        --localSize[X_DIM];
        --localSize[Y_DIM];
      }
    return;

  default:
    return;
    }
}

double
maxValue(int* values, const size_t SIZE)
{
  timeUtils::Clock timer;
  timer.start();

  for (size_t i = 1; i < SIZE; i++)
    values[0] = std::max(values[0], values[i]);

  timer.stop();
  return timer.getTimeInSeconds();
}

void
initCL(cl::Context & context, const cl_device_type CL_TYPE,
    std::vector<cl::Device> & devices, cl::CommandQueue & cmdQ,
    cl::Program & program, cl::Kernel & kernel)
{
  /*** Initialisiere OpenCL-Objekte ***/

  /*** Erstelle OpenCL-Context und CommandQueue ***/
  context = cl::Context(CL_TYPE);
  devices = context.getInfo<CL_CONTEXT_DEVICES> ();
  //device = devices.front();
  cmdQ = cl::CommandQueue(context, devices[0], CL_QUEUE_PROFILING_ENABLE);
  /*** OpenCL-Quellcode einlesen ***/
  std::string src = readFile(KERNEL_PATH);
  cl::Program::Sources source;
  source.push_back(std::make_pair(src.data(), src.length()));
  /*** OpenCL-Programm aus Quellcode erstellen ***/
  program = cl::Program(context, source);
  try
    {
      program.build(devices);
    }
  catch (cl::Error & err)
    {
      ;
      Logger::logDebug(
          "initCL",
          Logger::sStream << err.what() << "\nBuild-Log fuer \""
              << devices.front().getInfo<CL_DEVICE_NAME> () << "\":\n"
              << program.getBuildInfo<CL_PROGRAM_BUILD_LOG> (devices.front()));
      throw err;
    }
  kernel = cl::Kernel(program, "maxInt");
}

double
maxValueCL(const cl_device_type CL_TYPE, int* values, const size_t SIZE)
{
  const std::string METHOD("maxValueCL");
  timeUtils::Clock timer;

  std::vector<cl::Platform> platforms;
  std::vector<cl::Device> devices;
  cl::Context context;
  cl::Program program;
  cl::Program::Sources source;
  cl::Kernel kernel;
  cl::CommandQueue cmdQ;

  try
    {
      cl_int status = CL_SUCCESS;
      initCL(context, CL_TYPE, devices, cmdQ, program, kernel);

      /*** Ausgabe von Informationen ueber gewaehltes OpenCL-Device ***/
      Logger::logInfo(
          METHOD,
          Logger::sStream << "max compute units: " << devices[0].getInfo<
              CL_DEVICE_MAX_COMPUTE_UNITS> ());
      Logger::logInfo(
          METHOD,
          Logger::sStream << "max work item sizes: " << devices[0].getInfo<
              CL_DEVICE_MAX_WORK_ITEM_SIZES> ()[0]);
      Logger::logInfo(
          METHOD,
          Logger::sStream << "max work group sizes: " << devices[0].getInfo<
              CL_DEVICE_MAX_WORK_GROUP_SIZE> ());
      Logger::logInfo(
          METHOD,
          Logger::sStream << "max global mem size (KB): "
              << devices[0].getInfo<CL_DEVICE_GLOBAL_MEM_SIZE> () / 1024);
      Logger::logInfo(
          METHOD,
          Logger::sStream << "max local mem size (KB): " << devices[0].getInfo<
              CL_DEVICE_LOCAL_MEM_SIZE> () / 1024);

      /*** Erstellen und Vorbereiten der Daten ***/
      timer.start();
      cl::Buffer vBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
          sizeof(cl_int) * SIZE, &values[0], &status);
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "cl::Buffer values");
        }
      cmdQ.finish();

      /*** Arbeitsgr��en berechnen ***/
      // Anzahl der Work-Items = globalSize
      // Work-Items pro Work-Group = localSize
      const size_t MAX_GROUP_SIZE = devices[0].getInfo<
          CL_DEVICE_MAX_WORK_GROUP_SIZE> ();
      size_t globalSize[1] =
        { SIZE };
      size_t localSize[1] =
        { MAX_GROUP_SIZE };
      calcWorkGroupSize(1, globalSize, localSize, MAX_GROUP_SIZE);
      Logger::logDebug(METHOD,
          Logger::sStream << "globalSize[0]: " << globalSize[0]);
      Logger::logDebug(METHOD,
          Logger::sStream << "localSize[0]: " << localSize[0]);

      /*** Kernel-Argumente setzen  ***/
      status = kernel.setArg(0, vBuffer);
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "Kernel.SetArg");
        }
      status = kernel.setArg(1, (cl_uint)(SIZE));
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "Kernel.SetArg");
        }
      status = kernel.setArg(2, (cl_uint)(localSize[0]));
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "Kernel.SetArg");
        }

      status = kernel.setArg(3, sizeof(cl_int) * localSize[0], NULL);
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "Kernel.SetArg");
        }

      /*** Kernel ausfuehren und auf Abarbeitung warten ***/
      cl::KernelFunctor func = kernel.bind(cmdQ, cl::NDRange(globalSize[0]),
          cl::NDRange(localSize[0]));

      cl::Event event = func();

      cmdQ.finish();

      /*** Daten vom OpenCL-Device holen ***/
      status = cmdQ.enqueueReadBuffer(vBuffer, true, 0, sizeof(cl_int) * SIZE,
          &values[0]);
      if (status != CL_SUCCESS)
        {
          throw cl::Error(status, "CommandQueue.enqueueReadBuffer");
        }

      timer.stop();

      double runtimeKernel = 0;
      runtimeKernel += event.getProfilingInfo<CL_PROFILING_COMMAND_END> ();
      runtimeKernel -= event.getProfilingInfo<CL_PROFILING_COMMAND_START> ();
      Logger::log(METHOD, TIME,
          Logger::sStream << "timeKernel=" << 1.0e-9 * runtimeKernel << ";");
    }
  catch (cl::Error& err)
    {
      Logger::logError(METHOD, Logger::sStream << err.what());
    }
  catch (std::exception& err)
    {
      Logger::logError(METHOD, Logger::sStream << err.what());
    }

  return timer.getTimeInSeconds();
}

void
fillVector(int* vec, const size_t SIZE)
{
  srand(time(NULL));
  for (size_t i = 0; i < SIZE; ++i)
    {
      vec[i] = rand() % 1024;
    }
}

bool
checkArguments(int argc, char** argv)
{
  if (argc < 4)
    {
      std::cout << "Argumente: " << ALL << "|" << DEBUG << "|" << NORMAL << "|"
          << TIMEL << "|" << ERRORL << " " << SINGLE << "|" << CPU << "|"
          << GPU << " <size>" << std::endl;
      return false;
    }

  char log = *argv[1];
  type = *argv[2];
  size = atoi(argv[3]);

  switch (log)
    {
  case ALL:
    Logger::setLogMask(Level::ALL);
    break;
  case DEBUG:
    Logger::setLogMask(Level::DEBUG);
    break;
  case NORMAL:
    Logger::setLogMask(Level::NORMAL);
    break;
  case TIMEL:
    Logger::setLogMask(TIME);
    break;
  case ERRORL:
    Logger::setLogMask(Level::ERR);
    break;
  default:
    Logger::setLogMask(Level::NORMAL);
    }

  return true;
}

int
main(int argc, char** argv)
{
  if (!checkArguments(argc, argv))
    return EXIT_FAILURE;

  const std::string METHOD("main");
  double runtime = -1.0;

  Logger::logInfo(METHOD, Logger::sStream << "type = " << type);
  Logger::logInfo(METHOD, Logger::sStream << "size = " << size);
  Logger::logInfo(METHOD,
      Logger::sStream << "RAM (KB) > " << (size * sizeof(int)) / 1024);

  /*** Erzeugen der Daten***/
  int* values = (int*) malloc(size * sizeof(int));

  fillVector(values, size);

  values[(size_t) (size / 2)] = 4242;
  values[size - 1] = 7331;
  values[(size_t) (size / 3)] = 2323;

  /*** Implementierung waehlen ***/
  switch (type)
    {
  case SINGLE:
    runtime = maxValue(values, size);
    break;
  case GPU:
    runtime = maxValueCL(CL_DEVICE_TYPE_GPU, values, size);
    break;
  case CPU:
    runtime = maxValueCL(CL_DEVICE_TYPE_CPU, values, size);
    break;
  default:
    ;
    Logger::logError(METHOD,
        Logger::sStream << "Falsches Argument \"" << type << "\"");
    }

  // ACHTUNG getMatrixString wuerde immer berechnet werden! (bad_alloc moeglich)
  // if (runtime > -1 && (result.rows < 10 && result.cols < 10))
  Logger::log(METHOD, TIME, Logger::sStream << "time=" << runtime << ";");

  for (size_t i = 0; i < 15 && i < size; i++)
    std::cout << values[i] << std::endl;

  free(values);

  return EXIT_SUCCESS;
}
