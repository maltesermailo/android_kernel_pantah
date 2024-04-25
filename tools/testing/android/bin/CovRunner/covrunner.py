import argparse
import glob
import pandas as pd
import shlex
import tomllib
import subprocess
import datetime
from bs4 import BeautifulSoup
from enum import Enum
from pathlib import Path


STG_CNT = 4

class Cov(Enum):
    Run = 1
    CollectInfo = 2
    CollectHtml = 4
    Collect = 6
    Parse = 8
    All = 15

    def __str__(self):
        return self.name


def check_file(file:Path):
    if not file.exists():
        print(f'{file} does not exit.')
        return False
    
    if not file.is_file():
        print(f'{file} is not a file.')
        return False
    
    return True


def collect_infos(args, active=False):
    if not active:
        print('[Collect]Info Task was skipped')
        return True
    
    try:
        validate_env_before_collect_info(args)
    except NotADirectoryError as nad:
        print(f'[Collect]{nad}')
        return False
    except FileNotFoundError as fna:
        print(f'[Collect]{fna}')
        return False
        
    gen_cov_info(args["kernel_tree"], args["output_dir"])
    return True

def collect_htmls(args, active=False):
    if not active:
        print('[Collect]Html Task was skipped')
        return True
    
    try:
        validate_env_before_collect_html(args)
    except NotADirectoryError as nad:
        print(f'[Collect]{nad}')
        return False
    except FileNotFoundError as fna:
        print(f'[Collect]{fna}')
        return False
    
    gen_html(args['output_dir'])
    return True


def execute(cmd, cwd = None):
    popen = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE,stderr=subprocess.STDOUT, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line 
    
    popen.stdout.close()
    return_code = popen.wait()
    
    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)


def gen_cov_info(kernel_dir, output_dir):
    file_paths = glob.glob(f'{output_dir}/**/*_kernel_coverage_*.tar.gz', recursive= True)
    # ./common/tools/testing/android/bin/create-tracefile.py -t tf-logs/ --include net/socket.c
    trace_script = kernel_dir / Path('common/tools/testing/android/bin/create-tracefile.py')
    
    for idx, file_path in enumerate(file_paths, 1):
        target_dir = Path(file_path).parent
        cmd = shlex.split(f'{trace_script} -t {target_dir}')
        
        for line in execute(cmd, cwd= str(target_dir)):
            print(line, end="")
        print(f'{target_dir.name}: cov.info is generated.')


def gen_html(output_dir:Path):
    file_paths = glob.glob(f'{output_dir}/**/cov.info', recursive= True)
    # genhtml --branch-coverage --synthesize-missing --ignore-errors source -o ~/coverage/RemoteConnectionTest/html ~/coverage/RemoteConnectionTest/cov.info

    for idx, file_path in enumerate(file_paths, 1):
        target_dir = Path(file_path).parent
        output_file = target_dir / Path('html')
        cmd = shlex.split(f'genhtml --branch-coverage --synthesize-missing --ignore-errors source -o {output_file} {file_path}')
        
        for line in execute(cmd, cwd= str(target_dir)):
            print(line, end="")
        print(f'{target_dir.name}: html is generated.')


def get_env_data_after_check(exec_dir: Path):
    if not check_file(exec_dir / Path("run.sh")):
        return None
    
    config = exec_dir / Path("cfg.toml")   
    if not check_file(config):
        return None
    
    with open(config, "rb") as f:
        data = tomllib.load(f)

    for key, p in data['path'].items():
        if not p:
            print(f'{key} should not be an empty string.')
            return None
        
        if not Path(p).expanduser().exists():
            print(f'{p} does not exist.')
            return None
          
    try:
        args = init_args(data)
    except ValueError as v:
        print(f"{v}")
        return None
    except IndexError as i:
        print(f"{i}")
        return None
      
    return args


def init_args(data:dict):
    parser = argparse.ArgumentParser(description="Assign the workflow type")
    parser.add_argument("-t", "--type",dest = 'cov_type', choices=list(Cov), type=lambda cov: Cov[cov], required=True, help="Workflow Setting")
    args = vars(parser.parse_args())
    args['execute_dir'] = Path(__file__).parent
  
    tmp_args, params = {}, ('path', 'test', 'env')
    for cfg_key in params:
        tmp_args.update(
            {k: (Path(v).expanduser() if cfg_key == 'path' else v) 
             for k, v in data[cfg_key].items()}
        )

    for k, v in tmp_args.items():
        if isinstance(v, list) and not v:
            raise IndexError(f'{k}: test classes should not be empty.')  
        if not v:
            raise ValueError(f'{k} should not be an empty string.') 
    
    args.update(tmp_args)
    return args

def init_flags(cov_type:Cov, stage_cnt):
    flags = [False] * stage_cnt
    active_mask = f'{cov_type.value:b}'[::-1]
    for idx, bin in enumerate(active_mask):
        flags[idx] = bool(int(bin))
    return flags


def output_diretories(args):
    for arg in args['class']:
        yield arg.split('.')[-1]


def validate_env_before_collect_info(args):
    for dir in output_diretories(args):        
        target_dir:Path = args['output_dir'] / Path(dir)
        if not target_dir.exists() or not target_dir.is_dir():
            raise NotADirectoryError(f'{target_dir} doesn\'t exist')
        
        gz_files:Path  = glob.glob(f'{target_dir}/*_kernel_coverage_*.tar.gz', recursive= True)
        if not gz_files:
            raise FileNotFoundError('Can not found the *_kernel_coverage_*.tar.gz file')

def validate_env_before_collect_html(args):
    for dir in output_diretories(args):        
        target_dir:Path = args['output_dir'] / Path(dir)
        if not target_dir.exists() or not target_dir.is_dir():
            raise NotADirectoryError(f'{target_dir} doesn\'t exist')
        
        cov_info:Path  = target_dir / Path('cov.info')
        if not cov_info.exists() or not cov_info.is_file():
            raise FileNotFoundError(f'{dir}:Can not found the cov.info file')


def validate_env_before_run(output_root:Path):
    if output_root.exists() and not output_root.is_dir():
        raise NotADirectoryError(f'[Run]{output_root} is not a directory.')
    
    if not output_root.exists():
        output_root.mkdir(parents=True, exist_ok=True)
     
    if any(output_root.iterdir()):
        raise SystemError('The output directory must be empty')


def validate_env_before_parse(args):
    for dir in output_diretories(args):        
        target_dir:Path = args['output_dir'] / Path(dir)
        if not target_dir.exists() or not target_dir.is_dir():
            raise NotADirectoryError(f'{target_dir} doesn\'t exist')
        
        html:Path  = target_dir / Path('html/index.html')
        if not html.exists() or not html.is_file():
            raise FileNotFoundError(f'{dir}:Can not found the html file')
        
        exec_time_csv:Path  = target_dir / Path('cov_exec.csv')
        if not exec_time_csv.exists() or not exec_time_csv.is_file():
            raise FileNotFoundError(f'{dir}:Can not found the cov_exec.csv file')
def run(args, active=False):
    if not active:
        print('Stage Run was skipped.')
        return True

    try:
        validate_env_before_run(args['output_dir'])
    except NotADirectoryError as nd:
        print(f'{nd}')
        return False
    except SystemError as sys:
        print(f'{sys}')
        return False

    keys = ('platform_tree', 'output_dir', 'build_target', 'android_serial')
    script_args = [str(args[k]) for k in keys]
    class_args = [f'{args["module"]}:{c}' for c in args['class']]
    run_script = args['execute_dir'] / Path('run.sh') 
    cmd = shlex.split(f'bash {run_script}') + script_args + class_args

    for line in execute(cmd):
       print(line, end="")
    
    return True


def _parse_cov_metrics(html_path, class_name:str, exec_time:str):
      
    with open(html_path) as fp:
        soup = BeautifulSoup(fp, 'lxml')

    center = soup.find('center')
    table = center.find('table')
    trs = table.find_all('tr')

    columns = [
        'class',
        'exec_time', 
        'dirtory', 
        'lines_total', 
        'lines_hit', 
        'branch_total', 
        'branch_hit', 
        'function_total', 
        'function_hit'
    ]
    df = pd.DataFrame(columns=columns)
    for tr in trs:
        dir = tr.find('td', {'class': 'coverFile'})
        if not dir:
            continue
        
        covers = tr.find_all('td', {'class': 'coverNumDflt'})
        l = list(map(lambda n: n.get_text(), covers))     
        df.loc[len(df.index)] = (class_name, exec_time, dir.get_text(), *l)
    return df


def parse(args, active=False):
    if not active:
        print('Stage Parse was skipped.')
        return True
    
    try:
        validate_env_before_parse(args)
    except NotADirectoryError as nad:
        print(f'[Parse]{nad}')
        return False
    except FileNotFoundError as fna:
        print(f'[Parse]{fna}')
        return False
    
    root = args['output_dir']
    exec_stat_files = glob.glob(f'{root}/**/cov_exec.csv', recursive=True)
    dfs = []
    for idx, stat in enumerate(exec_stat_files, 1):
        df = pd.read_csv(stat, header= None,names=['class', 'exec_time'])
        target_dir = Path(stat).parent 
        
        html_file = target_dir / Path('html/index.html')    
        df = _parse_cov_metrics(html_file, df.loc[0, 'class'], df.loc[0, 'exec_time'])
        dfs.append(df)
    
    output_file = root / f'html_stats_{datetime.datetime.now().strftime("%Y_%m_%d_%H%M%S")}.csv'   
    df_final = pd.concat(dfs, ignore_index= True)
    df_final.to_csv(output_file, index=False)
    return True


def main():
    exec_dir = Path(__file__).parent 
    if (args:=get_env_data_after_check(exec_dir)) is None:
        return

    flags = init_flags(args['cov_type'], STG_CNT)

    result = run(args, active=flags[0])   
    if not result:
        print("An error occurred on Run Stage. Terminating program...")
        exit(1)
    
    result = collect_infos(args, active=flags[1])
    if not result:
        print("An error occurred on Collect-Info Stage. Terminating program...")
        exit(1)

    result = collect_htmls(args, active=flags[2])
    if not result:
        print("An error occurred on Collect-Html Stage. Terminating program...")
        exit(1)

    result = parse(args, active=flags[3])
    if not result:
        print("An error occurred on Parse Stage. Terminating program...")
        exit(1)


if __name__ == '__main__':
    main()