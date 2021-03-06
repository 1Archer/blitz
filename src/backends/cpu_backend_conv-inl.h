#ifndef SRC_BACKENDS_CPU_BACKEND_CONV_INL_H_
#define SRC_BACKENDS_CPU_BACKEND_CONV_INL_H_

static void Convolution2DForwardFunc(
  const CPUTensor<DType>* input,
  const CPUTensor<DType>* filter,
  CPUTensor<DType>* output,
  ConvolutionContext<CPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(filter->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  CPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  output->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  timeval start, end;
  double elapsed_time;
  BLITZ_CPU_TIMER_START(elapsed_time, start);
  #endif  // BLITZ_PERFORMANCE
  switch (context->algorithm()) { // NCHW & NHWC
    case BLITZ_CONVOLUTION_BLAS_GEMM_BATCH: {
      #pragma omp parallel private(nCHW, nKPQ)
      {
        const size_t tid = omp_get_thread_num();
        const size_t workspace_unpack_offset = tid * CRS * PQ;
        DType* workspace_unpack_slice = workspace->Slice(workspace_unpack_offset);
        #pragma omp for
        for (size_t n = 0; n < NIN; ++n) {
          nCHW = n * CHW;
          nKPQ = n * KPQ;
          utils::Unpack2DDispatch<CPUTensor, DType>(
            input->Slice(nCHW),
            workspace_unpack_slice,
            C, H, W,
            R, S,
            P, Q,
            pad_h, pad_w,
            str_h, str_w,
            input->data_layout());
          utils::Convolution2DForwardGEMMDispatch<CPUTensor, DType>(
            workspace_unpack_slice,
            filter->data(),
            output->Slice(nKPQ),
            K, PQ, CRS,
            input->data_layout(),
            output->data_layout());
        }
      }
      break;
    }
    case BLITZ_CONVOLUTION_BLAS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        utils::Unpack2DDispatch<CPUTensor, DType>(
          input->Slice(nCHW),
          workspace->data(),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
        utils::Convolution2DForwardGEMMDispatch<CPUTensor, DType>(
          workspace->data(),
          filter->data(),
          output->Slice(nKPQ),
          K, PQ, CRS,
          input->data_layout(),
          output->data_layout());
      }
      break;
    }
    case BLITZ_CONVOLUTION_NAIVE_DIRECT: {
      if (input->data_layout() != output->data_layout()) {
        LOG(FATAL) << "Not supported data layout transformation from " <<
          input->data_layout() << " to " << output->data_layout() << " for direct convolution!";
      }
      switch (input->data_layout()) {
        case BLITZ_BUFFER_NCHW:
          utils::ConvolutionForwardNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NCHW>(
            input->data(),
            filter->data(),
            output->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        case BLITZ_BUFFER_NHWC:
          utils::ConvolutionForwardNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NHWC>(
            input->data(),
            filter->data(),
            output->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        default:
          LOG(FATAL) << "Not supported data layout!" << input->data_layout();
      }
      break;
    }
    case BLITZ_CONVOLUTION_VECTOR_DIRECT: {
      if (input->data_layout() != output->data_layout()) {
        LOG(FATAL) << "Not supported data layout transformation from " <<
          input->data_layout() << " to " << output->data_layout() << " for direct convolution!";
      }
      switch (input->data_layout()) {
        case BLITZ_BUFFER_NCHW:
          LOG(FATAL) << "Not supported data layout!" << input->data_layout();
        case BLITZ_BUFFER_NHWC:
          utils::ConvolutionForwardVectorImpl<CPUTensor, DType, BLITZ_BUFFER_NHWC>(
            input->data(),
            filter->data(),
            output->data(),
            workspace->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        default:
          LOG(FATAL) << "Not supported data layout!" << input->data_layout();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_CPU_TIMER_END(elapsed_time, start, end);
  BLITZ_CPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

static void Convolution2DBackwardFunc(
  const CPUTensor<DType>* output,
  const CPUTensor<DType>* filter,
  CPUTensor<DType>* input,
  ConvolutionContext<CPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(filter->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  CPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  input->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  timeval start, end;
  double elapsed_time;
  BLITZ_CPU_TIMER_START(elapsed_time, start);
  #endif  // BLITZ_PERFORMANCE
  switch (context->algorithm()) {
    case BLITZ_CONVOLUTION_BLAS_GEMM_BATCH: {
      #pragma omp parallel private(nCHW, nKPQ) 
      {
        const size_t tid = omp_get_thread_num();
        const size_t workspace_unpack_offset = tid * CRS * PQ;
        #pragma omp for
        for (size_t n = 0; n < NIN; ++n) {
          nCHW = n * CHW;
          nKPQ = n * KPQ;
          utils::Convolution2DBackwardGEMMDispatch<CPUTensor, DType>(
            filter->data(),
            output->Slice(nKPQ),
            workspace->Slice(workspace_unpack_offset),
            K, PQ, CRS,
            input->data_layout(),
            output->data_layout());
          utils::Pack2DDispatch<CPUTensor, float>(workspace->Slice(workspace_unpack_offset),
            input->Slice(nCHW),
            C, H, W,
            R, S,
            P, Q,
            pad_h, pad_w,
            str_h, str_w,
            input->data_layout());
        }
      }
      break;
    }
    case BLITZ_CONVOLUTION_BLAS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        utils::Convolution2DBackwardGEMMDispatch<CPUTensor, float>(
          filter->data(),
          output->Slice(nKPQ),
          workspace->data(),
          K, PQ, CRS,
          input->data_layout(),
          output->data_layout());
        utils::Pack2DDispatch<CPUTensor, DType>(workspace->data(),
          input->Slice(nCHW),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
      }
      break;
    }
    case BLITZ_CONVOLUTION_NAIVE_DIRECT: {
      if (input->data_layout() != output->data_layout()) {
        LOG(FATAL) << "Not supported data layout transformation for direct convolution!";
      }
      switch (input->data_layout()) {
        case BLITZ_BUFFER_NCHW:
          utils::ConvolutionBackwardNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NCHW>(
            output->data(),
            filter->data(),
            input->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        case BLITZ_BUFFER_NHWC:
          utils::ConvolutionBackwardNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NHWC>(
            output->data(),
            filter->data(),
            input->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        default:
          LOG(FATAL) << "Not supported data layout!" << input->data_layout();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_CPU_TIMER_END(elapsed_time, start, end);
  BLITZ_CPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

static void Convolution2DUpdateFunc(
  const CPUTensor<DType>* input,
  const CPUTensor<DType>* output,
  CPUTensor<DType>* update,
  ConvolutionContext<CPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(update->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  CPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  workspace->Fill(0);
  update->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  timeval start, end;
  double elapsed_time;
  BLITZ_CPU_TIMER_START(elapsed_time, start);
  #endif  // BLITZ_PERFORMANCE
  switch (context->algorithm()) {
    case BLITZ_CONVOLUTION_BLAS_GEMM_BATCH: {
      #pragma omp parallel private(nCHW, nKPQ)
      {
        const size_t tid = omp_get_thread_num();
        const size_t workspace_unpack_size = CRS * PQ;
        const size_t workspace_update_size = K * CRS;
        const size_t workspace_unpack_offset = tid * (workspace_unpack_size + workspace_update_size);
        const size_t workspace_update_offset = workspace_unpack_offset + workspace_unpack_size;
        #pragma omp for
        for (size_t n = 0; n < NIN; ++n) {
          nCHW = n * CHW;
          nKPQ = n * KPQ;
          utils::Unpack2DDispatch<CPUTensor, DType>(input->Slice(nCHW),
            workspace->Slice(workspace_unpack_offset),
            C, H, W,
            R, S,
            P, Q,
            pad_h, pad_w,
            str_h, str_w,
            input->data_layout());
          utils::Convolution2DUpdateGEMMDispatch<CPUTensor, DType>(
            workspace->Slice(workspace_unpack_offset),
            output->Slice(nKPQ),
            workspace->Slice(workspace_update_offset),
            K, CRS, PQ,
            input->data_layout(),
            output->data_layout());
        }
        for (size_t i = 0; i < update->size(); ++i) {
          #pragma omp atomic
          (*update)[i] += *(workspace->Slice(workspace_update_offset + i));
        }
      }
      break;
    }
    case BLITZ_CONVOLUTION_BLAS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        utils::Unpack2DDispatch<CPUTensor, DType>(input->Slice(nCHW),
          workspace->data(),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
        utils::Convolution2DUpdateGEMMDispatch<CPUTensor, DType>(
          workspace->data(),
          output->Slice(nKPQ),
          update->data(),
          K, CRS, PQ,
          input->data_layout(),
          output->data_layout());
      }
      break;
    }
    case BLITZ_CONVOLUTION_NAIVE_DIRECT: {
      if (input->data_layout() != output->data_layout()) {
        LOG(FATAL) << "Not supported data layout transformation for direct convolution!";
      }
      switch (input->data_layout()) {
        case BLITZ_BUFFER_NCHW:
          utils::ConvolutionUpdateNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NCHW>(
            input->data(),
            output->data(),
            update->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        case BLITZ_BUFFER_NHWC:
          utils::ConvolutionUpdateNaiveImpl<CPUTensor, DType, BLITZ_BUFFER_NHWC>(
            input->data(),
            output->data(),
            update->data(),
            NIN,
            C, H, W,
            R, S,
            K, P, Q,
            pad_h, pad_w,
            str_h, str_w);
          break;
        default:
          LOG(FATAL) << "Not supported data layout!" << input->data_layout();
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_CPU_TIMER_END(elapsed_time, start, end);
  BLITZ_CPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

#endif  // SRC_BACKENDS_CPU_BACKEND_CONV_INL_H_
