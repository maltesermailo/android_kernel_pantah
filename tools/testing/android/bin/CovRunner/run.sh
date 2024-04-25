PLATFORM_TREE="${1}"
OUT_DIR="${2}"
BUILD_TARGET="${3}"
SERIAL_PORT=${4}

echo "[PLATFORM_TREE] " ${PLATFORM_TREE}
echo "[OUTPUT_DIR] " ${OUT_DIR}
echo "[BUILD_TARGET] " ${BUILD_TARGET}
echo "[ANDROID_SERIAL] " ${SERIAL_PORT}


[[ -f ${PLATFORM_TREE}/build/envsetup.sh ]] || exit

cd ${PLATFORM_TREE}
source build/envsetup.sh
lunch ${BUILD_TARGET}

export ANDROID_SERIAL=${SERIAL_PORT}


for class in "${@:5}"; do
  class_name=$(cut -d. -f4 <<< "${class}")
  file_dir=$(atest ${class} -- --coverage --coverage-toolchain GCOV_KERNEL --auto-collect GCOV_KERNEL_COVERAGE | grep "Atest results and logs directory" | cut -d: -f2 | xargs)
  gz_file_path=$(find ${file_dir} -name *_kernel_coverage_*.tar.gz)
  gz_file_name=$(basename "${gz_file_path}")

  target_dir="${OUT_DIR}/${class_name}"
  mkdir -p "${target_dir}"
  cp "${gz_file_path}" "${target_dir}/${gz_file_name}"
  
  exec_log=$(find "${file_dir}" -name end_host_log_*.txt)
  exec_time=$(cat ${exec_log} | grep "Total Run time" | cut -d':' -f2 | xargs)  
  echo "${class_name},${exec_time}" > "${target_dir}/cov_exec.csv"
  echo "${class_name}: The testing is completed, and generated the tar file."
done
    