#!/usr/bin/env python3
"""Require the completed read-only job from the exact reviewed SmartBox commit."""
import json
import os
import re
import subprocess
import sys

JOB = 'SmartBox private SFTP preflight or firmware publication'
STEP = 'Record successful read-only SmartBox preflight'


def accepted(run, jobs, commit):
    if (run.get('path') != '.github/workflows/deploy.yml'
            or run.get('head_sha') != commit or run.get('head_branch') != 'main'
            or run.get('event') != 'workflow_dispatch' or run.get('conclusion') != 'success'):
        return False
    return any(job.get('name') == JOB and job.get('conclusion') == 'success'
               and any(step.get('name') == STEP and step.get('conclusion') == 'success'
                       for step in job.get('steps', []))
               for job in jobs)


def main():
    commit = os.environ.get('EXPECTED_COMMIT', '')
    run_id = os.environ.get('PREFLIGHT_RUN_ID', '')
    if (not re.fullmatch(r'[0-9a-f]{40}', commit) or commit != os.environ.get('GITHUB_SHA')
            or os.environ.get('GITHUB_RUN_ATTEMPT') != '1'
            or not re.fullmatch(r'[1-9][0-9]{0,19}', run_id)):
        return 1
    prefix = 'repos/ks-house/smartbox/actions/runs/' + run_id
    try:
        run = json.loads(subprocess.check_output(['gh', 'api', prefix]))
        attempt = int(run['run_attempt'])
        jobs = json.loads(subprocess.check_output(['gh', 'api',
                          f'{prefix}/attempts/{attempt}/jobs?per_page=50']))['jobs']
        return 0 if accepted(run, jobs, commit) else 1
    except (ValueError, TypeError, KeyError, OSError, subprocess.SubprocessError):
        return 1


if __name__ == '__main__':
    result = main()
    print('Reviewed read-only preflight matched.' if result == 0 else
          'Publication denied: reviewed commit or read-only preflight did not match.')
    sys.exit(result)
