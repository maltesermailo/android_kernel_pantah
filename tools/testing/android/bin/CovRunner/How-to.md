# Prequisites
1. LCOV Tool  
LCOV is a coverage measurement tool for Android Kernel. See the github [site](https://github.com/linux-test-project/lcov/releases) to install the tool.

2. Virtual Environment  
Use the `venv` command to create the virtual environment
    ```
    $ python -m venv .venv
    $ source ./.venv/bin/activate
    ```
3. Python Package Dependencies  
    Install those packages listed in the requrirements.txt. 
    ```
    $ pip install -r requirements.txt
    ```
3. Kernel Tree  
    Build the kernel with `--gcov` option.
    ```
    $ bazel run --gcov //common-modules/virtual-device:virtual_device_x86_64_dist
    ```

4. Platform Tree  

5. Acloud Instance  
    Create the Acloud instance and make sure it is connected
    ```
    $ acloud create --local-image \
        --local-kernel-image $ANDROID_KERNEL_BUILD_ROOT/out/virtual_device_x86_64/dist/
    ```

6. cfg.toml  
The `cfg.toml` should be under the CovRunner dirctory.

7. run.sh  
The `run.sh` should be under the CovRunner directory.

# Usage Steps

## Change your current directory to CovRunner
```
cd path/to/CovRunner
```

## Activate the virtual environment
```
$ cd path/to/CovRunner
$ source ./.venv/bin/activate
```

## Check the environment variable
Make sure you set up all the environment variables in the `cfg.toml`


## Run the script
```
python path/to/CovRunner/covrunner.py -t ${STAGE}
```

### STAGE type
 Choose one type on the following to run the script. e.g. `-t Run`
 
 * All  
   execute all the 4 stages:  
   Run -> CollectInfo -> CollectHtml -> Parse

 * Run  
   Run the `run.sh` script for generating the following:
    * kernel coverage .tar file. 
    * cov_exec_time.csv  
   
   the test classes which to run is configured in the cfg.toml, make sure the output directory is empty.

 * CollectInfo    
   Create a local tracefile named 'cov.info'

 * CollectHtml  
   Generate htmls for visualizing the metrics

 * Parse  
   Parse those metrics into csv.

