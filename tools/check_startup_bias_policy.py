"""Exercise the actual persistence helper/service with a host C compiler."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def extract(text,name):
    start=text.index(name+"(")
    start=text.rfind("\n",0,start)+1
    opening=text.index("{",start)
    depth=0
    for pos in range(opening,len(text)):
        depth+=(text[pos]=="{")-(text[pos]=="}")
        if not depth:
            return text[start:pos+1]
    raise ValueError("Unterminated function: "+name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc",default="gcc")
    args=parser.parse_args()
    application=(ROOT/"src/app/main.c").read_text(encoding="utf-8-sig")
    learning=(ROOT/"src/calibration/gyro_temp_learning.c").read_text(encoding="utf-8-sig")
    harness='''#include <assert.h>
#include <math.h>
#include <stdint.h>
static struct { float axis[3]; } app_gyro_scale={{2.f,4.f,.5f}};
static unsigned queued,auto_checks;
static uint8_t safe_to_save,saving,pending_save;
static float received[3];
static int gyro_bias_history_save_at_temp(const float b[3],float t) {
  (void)t;queued++;for(unsigned i=0;i<3;i++)received[i]=b[i];return 0;
}
static int gyro_temp_comp_save_allowed(uint32_t now,int explicit_request) {
  (void)now;(void)explicit_request;auto_checks++;return 1;
}
'''+extract(application,"app_save_gyro_bias")+'\n'+extract(learning,"gyro_temp_learning_environment")+'''
int main(void) {
  const float bias[3]={.2f,.8f,-.15f};
  assert(app_save_gyro_bias(bias,26.f)==0);
  assert(queued==EXPECT_QUEUE);
  if(EXPECT_QUEUE) {
    assert(fabsf(received[0]-.1f)<1e-6f);
    assert(fabsf(received[1]-.2f)<1e-6f);
    assert(fabsf(received[2]+.3f)<1e-6f);
  }
  gyro_temp_learning_environment(7200000U,1);
  assert(safe_to_save==1);
  assert(pending_save==EXPECT_AUTO && auto_checks==EXPECT_AUTO);
  return 0;
}
'''
    results=[]
    with tempfile.TemporaryDirectory(prefix="eskf-bias-policy-") as folder:
        folder=Path(folder)
        source=folder/"policy.c"
        source.write_text(harness,encoding="utf-8")
        for label,save,auto,updater,expected_queue,expected_auto in [
            ("ram-only",0,0,False,0,0),("legacy-default",1,1,False,1,1),
            ("protected-updater",1,1,True,0,0)]:
            exe=folder/(label+".exe")
            flags=[f"-DAPP_GYR_STARTUP_BIAS_SAVE_ENABLE={save}",f"-DAPP_TEMP_FLASH_AUTO_SAVE_ENABLE={auto}",
                   f"-DEXPECT_QUEUE={expected_queue}",f"-DEXPECT_AUTO={expected_auto}"]
            if updater:
                flags.append("-DAPP_USER_BL_UPDATE=1")
            subprocess.run([args.cc,"-std=c11","-O2",*flags,str(source),"-o",str(exe)],check=True,timeout=60)
            subprocess.run([str(exe)],check=True,timeout=10)
            results.append({"case":label,"result":"PASS"})
    print(json.dumps(results))

if __name__=="__main__":
    main()
