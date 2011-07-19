package cl_util;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;

import lightLogger.Logger;

import com.nativelibs4java.opencl.CLBuffer;
import com.nativelibs4java.opencl.CLEvent;
import com.nativelibs4java.opencl.CLException;
import com.nativelibs4java.opencl.CLKernel;
import com.nativelibs4java.opencl.CLMem;
import com.nativelibs4java.opencl.CLQueue;

public class CLFloat {

	private static final Class<CLFloat> CLAZZ = CLFloat.class;

	private CLInstance clInstance;

	public static final int MAX_BUFFER_ITEMS = 8192;
	private int BUFFER_ITEMS;
	private static final int SIZEOF_CL_FLOAT = 4;

	private static final String PREFIX = CLAZZ.getSimpleName();
	private static final String KERNEL_SUM = "sumFloat";
	private static final String KERNEL_PATH = "CLFloat.cl";

	private float buffer[];
	private int count;
	private float sum;
	private CLBuffer<FloatBuffer> resultBuffer;

	public CLFloat(CLInstance clInstance) {
		this.clInstance = clInstance;
		this.resetResult();
		this.resetBuffer();
	}

	public void resetBuffer(int bufferItems) {
		BUFFER_ITEMS = this.getOptimalItemCount(bufferItems);
		
		this.buffer = new float[BUFFER_ITEMS];
		this.count = 0;
		
		this.resultBuffer = this.clInstance.getContext().createFloatBuffer(
				CLMem.Usage.InputOutput, BUFFER_ITEMS);
	}

	public void resetBuffer() {
		this.resetBuffer(MAX_BUFFER_ITEMS);
	}

	private int getOptimalItemCount(int bufferItems) {
		if (bufferItems <= CLInstance.WAVE_SIZE)
			return CLInstance.WAVE_SIZE;
		else {
			int dual = CLInstance.WAVE_SIZE;
			while(dual < bufferItems && dual < MAX_BUFFER_ITEMS)
				dual *= 2;
			return dual;
		}
	}

	public void resetResult() {
		this.sum = 0;
		this.count = 0;
	}

	public void add(float value) {
		if (this.count < BUFFER_ITEMS) {
			this.buffer[count++] = value;
		} else {
			this.doSum(count);
			this.buffer[count++] = value;
		}		
	}

	private void doSum(int size) {
		int globalSize, localSize;
		globalSize = this.getOptimalItemCount(size);
		// fill offset with 0
		for (int i = size; i < globalSize; i++)
			buffer[i] = 0;
		size = globalSize;

		// get kernel and queue
		CLQueue cmdQ = this.clInstance.getQueue();
		CLKernel kernel = this.clInstance.getKernel(PREFIX, KERNEL_SUM);
		if (kernel == null) {
			kernel = this.clInstance
					.loadKernel(KERNEL_PATH, KERNEL_SUM, PREFIX);
		}

		try {
			// write buffer on device
			this.resultBuffer.write(cmdQ, FloatBuffer.wrap(this.buffer), true,
					new CLEvent[0]);

			// multiple rounds to sum each work group
			do {
				globalSize = this.getOptimalItemCount(size);
				// fill offset with 0 and write on device
				if (size < globalSize) {
					for (int i = 0; i < globalSize - size; i++)
						buffer[i] = 0;
					resultBuffer.write(cmdQ, size, globalSize - size,
							FloatBuffer.wrap(buffer), true, new CLEvent[0]);
				}
				localSize = this.clInstance.calcWorkGroupSize(globalSize);

				kernel.setArg(0, resultBuffer);
				kernel.setLocalArg(1, localSize * SIZEOF_CL_FLOAT);

				// Run kernel
				kernel.enqueueNDRange(cmdQ, new int[] { globalSize },
						new int[] { localSize }, new CLEvent[0]);

				size = globalSize / localSize;
			} while (globalSize > localSize && localSize > 1);

			cmdQ.finish();

			FloatBuffer resBuffer = ByteBuffer
					.allocateDirect(1 * SIZEOF_CL_FLOAT)
					.order(this.clInstance.getContext().getByteOrder())
					.asFloatBuffer();
			resultBuffer.read(cmdQ, 0, 1, resBuffer, true, new CLEvent[0]);
			resBuffer.rewind();

			this.sum += resBuffer.get(0);
		} catch (CLException err) {
			Logger.logError(CLAZZ, "OpenCL error:\n" + err.getMessage() + "():"
					+ err.getCode());
			err.printStackTrace();
		} catch (Exception err) {
			Logger.logError(CLAZZ, "Error:\n" + err.getMessage() + "()");
			err.printStackTrace();
		}
		// all values in buffer are summarized and can be overwritten
		this.count = 0;
	}

	public float getSum() {
		Logger.logTrace(CLAZZ, "getSum()");
		if (0 < this.count || this.count == BUFFER_ITEMS)
			this.doSum(this.count);
		return this.sum;
	}
}
