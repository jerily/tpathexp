package require tcltest 2
::tcltest::configure {*}$argv -singleproc true -testdir [file dirname [info script]]
exit [::tcltest::runAllTests]
