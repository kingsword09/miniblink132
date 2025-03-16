
const binding = process._linkedBinding('atom_common_shell');
const Shell = binding.Shell;
exports.Shell = Shell;

const shell = binding.Shell;
exports.shell = shell;