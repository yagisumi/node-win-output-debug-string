{
  "targets": [
    {
      'target_name': "win_output_debug_string",
      'sources': ["ext/win-output-debug-string.cc"],
      'include_dirs': ["<!@(node -p \"require('node-addon-api').include\")"],
      'dependencies': ["<!(node -p \"require('node-addon-api').gyp\")"],
      'defines': ['NAPI_DISABLE_CPP_EXCEPTIONS'],
      'conditions': [
        [
          'OS == "win"', {
            'libraries': ['-lAdvapi32'],
          }
        ]
      ],
    }
  ]
}
