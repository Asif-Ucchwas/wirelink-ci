// WireLink-CI pipeline: build, unit tests, update receiver on the Jetson, link tests over the cable.
pipeline {
  agent any
  options {
    timestamps()
    timeout(time: 15, unit: 'MINUTES')
  }
  environment {
    TX_IF   = 'enp131s0'              // sender port on this machine (Legion)
    RX_HOST = 'asif@10.42.0.11'       // Jetson Nano, reached over the ssh link
    RX_IF   = 'eth0'                  // receiver port on the Jetson
    RX_MAC  = '00:00:00:00:00:01'     // MAC the Jetson eth0 reports
  }
  stages {
    stage('Build') {
      steps { sh 'scons -Q' }
    }
    stage('Unit tests') {
      steps { sh 'scons -Q test' }
    }
    stage('Update receiver on Jetson') {
      steps {
        sh 'ssh -o BatchMode=yes "$RX_HOST" "cd ~/projects/wirelink-ci && git fetch -q origin && git reset -q --hard $GIT_COMMIT && scons -Q"'
      }
    }
    stage('Link tests') {
      steps {
        sh '''
          WL_TX_IF="$TX_IF" WL_RX_MAC="$RX_MAC" WL_RX_IF="$RX_IF" WL_RX_HOST="$RX_HOST" \
            python3 -m pytest -q tests/test_link.py --junitxml=build/link-tests.xml
        '''
      }
    }
  }
  post {
    always { junit allowEmptyResults: true, testResults: 'build/link-tests.xml' }
  }
}
