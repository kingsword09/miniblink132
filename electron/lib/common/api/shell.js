
const binding = process._linkedBinding('electron_common_shell');
const shell = binding.Shell;

if (typeof shell.trashItem !== 'function' && typeof shell.moveItemToTrash === 'function') {
    shell.trashItem = function (path) {
        return new Promise(function (resolve, reject) {
            try {
                if (shell.moveItemToTrash(path)) {
                    resolve();
                } else {
                    reject(new Error('Failed to move item to trash'));
                }
            } catch (error) {
                reject(error);
            }
        });
    };
}

exports.Shell = shell;
exports.shell = shell;
