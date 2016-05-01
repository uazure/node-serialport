{
  'targets': [
    {
      'target_name': 'serialport',
      'sources': [
        'src/serialport.cpp',
      ],
      'include_dirs': [
        '<!(node -e "require(\'nan\')")'
      ],
      'conditions': [
        ['OS=="win"',
          {
            'sources': [
              'src/serialport_win.cpp',
              'src/serialport_win_list',
              'src/win/disphelper.c',
              'src/win/enumser.cpp',
            ],
            'msvs_settings': {
              'VCCLCompilerTool': {
                'ExceptionHandling': '2',
                'DisableSpecificWarnings': [ '4530', '4506' ],
              }
            }
          }
        ],
        ['OS=="mac"',
          {
            'xcode_settings': {'OTHER_LDFLAGS': ['-framework CoreFoundation -framework IOKit']},
            'sources': [
              'src/serialport_mac_list.cpp',
            ],
            'sources!': [
              'src/serialport_unix_list.cpp',
            ]
          }
        ],
        ['OS!="win"',
          {
            'sources': [
              'src/serialport_unix.cpp',
              'src/serialport_unix_list.cpp',
              'src/serialport_poller.cpp'
            ],
          }
        ],
      ]
    }
  ],
}
