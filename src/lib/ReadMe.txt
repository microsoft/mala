ETW Tracing Manifest
================================

ETW tracing is used in this library. It is constructed based on the example found 

http://blogs.msdn.com/b/seealso/archive/2011/06/08/use-this-not-this-logging.aspx

To modify ETWTRacing.man, Run the Manifest Generator (ecmangen.exe) tool from the Windows SDK,

The following step is added to the prebuild event of the project. it requires ETWTracing.*
files to be writable. 

mc -um ETWTracing.man -h $(ProjectDir) -z ETWTracing

