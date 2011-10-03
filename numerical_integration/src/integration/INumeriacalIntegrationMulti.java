package integration;

import java.util.List;

import cl_util.ICLBufferedOperation;

public interface INumeriacalIntegrationMulti<T extends Number> extends INumeriacalIntegration<T>, ICLBufferedOperation<IInterval<T>> {
	
	public List<T> getIntegrals();	

}
