#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <chrono>
#include <memory>

namespace TCP {

	// 默认最大线程数
	constexpr int DEFAULT_MAX_THREADS = 4;
	// 默认最大队列任务数
	constexpr int DEFAULT_MAX_QUEUE_SIZE = 100;
	// 默认任务超时时间（-1表示永不超时）
	constexpr int DEFAULT_TASK_TIMEOUT = -1;

	// 线程池类
	class ThreadPool {
	public:
		// 构造函数
		// maxThreads: 最大线程数，默认4
		// maxQueueSize: 最大队列任务数，默认100
		// taskTimeoutSeconds: 任务超时时间（秒），-1表示永不超时，默认-1
		ThreadPool(int maxThreads = DEFAULT_MAX_THREADS,
				   int maxQueueSize = DEFAULT_MAX_QUEUE_SIZE,
				   int taskTimeoutSeconds = DEFAULT_TASK_TIMEOUT);
		// 析构函数
		~ThreadPool();

		// 添加任务到线程池
		// F: 函数类型
		// Args: 函数参数类型
		// f: 要执行的函数
		// args: 函数参数
		// 返回值: std::future，可用于获取任务执行结果
		template<typename F, typename... Args>
		auto AddTask(F&& f, Args&&... args)
			-> std::future<typename std::result_of<F(Args...)>::type>;

		// 设置最大线程数
		// num: 最大线程数，必须大于0
		void SetMaxThreads(int num);
		// 设置最大队列任务数
		// size: 最大队列任务数，必须大于0
		void SetMaxQueueSize(int size);
		// 设置任务超时时间
		// seconds: 超时时间（秒），-1表示永不超时
		void SetTaskTimeout(int seconds);

		// 获取当前活跃线程数
		int GetActiveThreadCount() const;
		// 获取队列中等待的任务数
		int GetQueuedTaskCount() const;
		// 获取最大线程数
		int GetMaxThreads() const;
		// 获取最大队列任务数
		int GetMaxQueueSize() const;
		// 获取任务超时时间
		int GetTaskTimeout() const;

		// 关闭线程池，等待所有任务完成
		void Shutdown();
		// 检查线程池是否已关闭
		bool IsShutdown() const;

		// 是否打印错误，默认true
		bool IsPrintError;
		// 错误信息
		std::string ErrorMsg;
		// 错误代码
		int ErrorCode;

	private:
		// 工作线程函数
		void WorkerThread();
		// 打印错误信息
		void PrintError();
		// 设置错误信息
		// msg: 错误信息
		// code: 错误代码
		void SetErrorMsg(const std::string& msg, int code);

		// 工作线程数组
		std::vector<std::thread> workers;
		// 任务队列
		std::queue<std::function<void()>> tasks;

		// 队列互斥锁（mutable用于const成员函数）
		mutable std::mutex queueMutex;
		// 条件变量，用于线程同步
		std::condition_variable condition;
		// 关闭条件变量（预留）
		std::condition_variable shutdownCondition;

		// 停止标志
		std::atomic<bool> stop;
		// 活跃线程数
		std::atomic<int> activeThreads;

		// 最大线程数
		int maxThreads;
		// 最大队列任务数
		int maxQueueSize;
		// 任务超时时间（秒）
		int taskTimeoutSeconds;
	};

	// 添加任务的模板函数实现
	template<typename F, typename... Args>
	auto ThreadPool::AddTask(F&& f, Args&&... args)
		-> std::future<typename std::result_of<F(Args...)>::type> {

		// 获取返回类型
		using return_type = typename std::result_of<F(Args...)>::type;

		// 创建打包任务，使用智能指针管理
		auto task = std::make_shared<std::packaged_task<return_type()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);

		// 获取future对象，用于获取任务执行结果
		std::future<return_type> res = task->get_future();

		{
			// 加锁
			std::unique_lock<std::mutex> lock(queueMutex);

			// 检查线程池是否已关闭
			if (stop) {
				SetErrorMsg("线程池已关闭，无法添加任务", -1);
				throw std::runtime_error("线程池已关闭");
			}

			// 检查队列是否已满
			if (tasks.size() >= static_cast<size_t>(maxQueueSize)) {
				SetErrorMsg("任务队列已满", -2);
				throw std::runtime_error("任务队列已满");
			}

			// 将任务加入队列
			tasks.emplace([task]() { (*task)(); });
		}

		// 通知一个等待的线程
		condition.notify_one();
		return res;
	}

}

#endif
