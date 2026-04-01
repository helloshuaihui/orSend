#include "ThreadPool.h"

namespace TCP {

	// 构造函数
	// 初始化线程池，创建指定数量的工作线程
	ThreadPool::ThreadPool(int maxThreads, int maxQueueSize, int taskTimeoutSeconds)
		: stop(false)
		, activeThreads(0)
		, maxThreads(maxThreads)
		, maxQueueSize(maxQueueSize)
		, taskTimeoutSeconds(taskTimeoutSeconds)
		, IsPrintError(true)
		, ErrorMsg("")
		, ErrorCode(0) {

		// 创建工作线程
		for (int i = 0; i < maxThreads; ++i) {
			workers.emplace_back([this] { WorkerThread(); });
		}
	}

	// 析构函数
	// 自动关闭线程池
	ThreadPool::~ThreadPool() {
		Shutdown();
	}

	// 工作线程主函数
	// 循环从任务队列中取出任务并执行
	void ThreadPool::WorkerThread() {
		while (true) {
			std::function<void()> task;

			{
				// 加锁保护任务队列
				std::unique_lock<std::mutex> lock(queueMutex);

				// 等待条件：停止标志为true 或 任务队列不为空
				condition.wait(lock, [this] {
					return stop || !tasks.empty();
				});

				// 如果停止标志为true且任务队列为空，则退出线程
				if (stop && tasks.empty()) {
					return;
				}

				// 从队列中取出任务
				if (!tasks.empty()) {
					task = std::move(tasks.front());
					tasks.pop();
					activeThreads++; // 活跃线程数加1
				}
			}

			// 执行任务
			if (task) {
				// 如果设置了超时时间
				if (taskTimeoutSeconds > 0) {
					// 使用async启动任务，并等待指定时间
					std::future<void> future = std::async(std::launch::async, task);
					if (future.wait_for(std::chrono::seconds(taskTimeoutSeconds)) == std::future_status::timeout) {
						SetErrorMsg("任务执行超时", -3);
					}
				}
				else {
					// 不设置超时，直接执行任务
					try {
						task();
					}
					catch (const std::exception& e) {
						SetErrorMsg(std::string("任务执行异常: ") + e.what(), -4);
					}
				}

				// 任务执行完成，活跃线程数减1
				activeThreads--;
			}
		}
	}

	// 设置最大线程数
	// num: 新的最大线程数，必须大于0
	void ThreadPool::SetMaxThreads(int num) {
		if (num <= 0) {
			SetErrorMsg("最大线程数必须大于0", -5);
			return;
		}

		std::unique_lock<std::mutex> lock(queueMutex);

		// 获取当前工作线程数
		int currentSize = static_cast<int>(workers.size());
		// 如果新的最大线程数大于当前线程数，则创建新线程
		if (num > currentSize) {
			for (int i = currentSize; i < num; ++i) {
				workers.emplace_back([this] { WorkerThread(); });
			}
		}
		// 更新最大线程数
		maxThreads = num;
	}

	// 设置最大队列任务数
	// size: 新的最大队列任务数，必须大于0
	void ThreadPool::SetMaxQueueSize(int size) {
		if (size <= 0) {
			SetErrorMsg("最大队列大小必须大于0", -6);
			return;
		}
		std::unique_lock<std::mutex> lock(queueMutex);
		maxQueueSize = size;
	}

	// 设置任务超时时间
	// seconds: 超时时间（秒），-1表示永不超时
	void ThreadPool::SetTaskTimeout(int seconds) {
		std::unique_lock<std::mutex> lock(queueMutex);
		taskTimeoutSeconds = seconds;
	}

	// 获取当前活跃线程数
	int ThreadPool::GetActiveThreadCount() const {
		return activeThreads.load();
	}

	// 获取队列中等待的任务数
	int ThreadPool::GetQueuedTaskCount() const {
		std::unique_lock<std::mutex> lock(queueMutex);
		return static_cast<int>(tasks.size());
	}

	// 获取最大线程数
	int ThreadPool::GetMaxThreads() const {
		return maxThreads;
	}

	// 获取最大队列任务数
	int ThreadPool::GetMaxQueueSize() const {
		return maxQueueSize;
	}

	// 获取任务超时时间
	int ThreadPool::GetTaskTimeout() const {
		return taskTimeoutSeconds;
	}

	// 关闭线程池
	// 设置停止标志，通知所有线程，等待所有线程退出
	void ThreadPool::Shutdown() {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			// 如果已经停止，则直接返回
			if (stop) {
				return;
			}
			// 设置停止标志
			stop = true;
		}

		// 通知所有等待的线程
		condition.notify_all();

		// 等待所有工作线程退出
		for (std::thread& worker : workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}

	// 检查线程池是否已关闭
	bool ThreadPool::IsShutdown() const {
		return stop.load();
	}

	// 打印错误信息
	void ThreadPool::PrintError() {
		if (IsPrintError && !ErrorMsg.empty()) {
			std::cout << "[THREADPOOL ERROR] " << ErrorMsg << " 错误代码:" << ErrorCode << std::endl;
		}
	}

	// 设置错误信息
	// msg: 错误信息
	// code: 错误代码
	void ThreadPool::SetErrorMsg(const std::string& msg, int code) {
		ErrorMsg = msg;
		ErrorCode = code;
		PrintError();
	}

}
