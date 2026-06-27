
const binding = process._linkedBinding('electron_common_shell');
const shell = binding.Shell;

if (typeof shell.openPath === 'function') {
    const nativeOpenPath = shell.openPath.bind(shell);
    shell.openPath = function (path) {
        return new Promise(function (resolve) {
            try {
                const result = nativeOpenPath(path);
                if (typeof result === 'string') {
                    resolve(result);
                } else if (result === false) {
                    resolve('Failed to open path');
                } else {
                    resolve('');
                }
            } catch (error) {
                resolve(error && error.message ? error.message : String(error));
            }
        });
    };
}

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
